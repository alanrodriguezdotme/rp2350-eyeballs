// The pendant firmware: one full-size eye at a steady 30 frames per second.
//
// By itself the eye looks around, blinks and moves its eyelids. It also reacts
// to deliberate movement: it looks toward the ground when tilted, lags behind
// a quick turn, goes wide-eyed when shaken and dizzy afterwards. Left still,
// it gets drowsy, closes, and the pendant turns itself off.
//
// Touch is locked until you ask, so clothes and hugs change nothing:
//   press and hold        unlock (or lock again)
// Then, until 10 seconds pass with no touch:
//   swipe left or right   next or previous eye style
//   swipe up or down      brighter or dimmer
//   tap                   one big eye, or a pair of eyes
//   double tap            show or hide a debug readout:
//     top:    the motion sensor's x y z in g, a letter for the current
//             reaction (S shaken, Z dizzy, D drowsy), and the battery percent
//     bottom: the frame rate, and BUSY, the milliseconds of each frame spent
//             working (the rest is spare)
//
// The style, layout and brightness are remembered while the board is off.

#include <stdio.h>
#include <string.h>

#include "pico/rand.h"
#include "pico/stdlib.h"

#include "DEV_Config.h"
#include "FT3168.h"
#include "GUI_Paint.h"
#include "behave.h"
#include "eye.h"
#include "eye_screen.h"
#include "motion.h"
#include "pcf85063.h"
#include "pwr_button.h"
#include "qmi8658.h"

#define FRAME_US 33333               // 30 frames per second
#define FRAME_S (FRAME_US / 1e6f)
#define STATS_EVERY 15               // frames between readout updates
#define TOP_Y 8                      // readout line above the eye

#define SWIPE_PX 60                  // a touch must travel this far to be a swipe
#define TAP_PX 20                    // and less than this to be a tap or a hold
#define HOLD_S 0.8f                  // press this long to lock or unlock
#define DOUBLE_TAP_S 0.35f           // a second tap this soon makes a double tap
#define RELOCK_S 10.0f               // unlocked touch locks itself after this
#define MESSAGE_FRAMES 45            // how long a message stays under the eye

#define BATTERY_EVERY_S 20.0f        // how often to read the battery
#define BATTERY_LOW_PERCENT 10
#define BATTERY_WARN_EVERY_S 60.0f   // how often to show the low-battery warning
#define AXP2101_ADDR 0x34
#define AXP2101_REG_BATTERY_PERCENT 0xA4

static const uint8_t brightness_levels[] = {12, 25, 40, 60, 80, 100};
#define BRIGHTNESS_COUNT ((int)sizeof(brightness_levels))
#define BRIGHTNESS_DEFAULT 3

static eye_renderer_t renderer;
static eye_behave_t behave;
static eye_motion_t motion;
// The current style's textures are copied to RAM: reading them from flash per
// pixel is slower. Iris first, then sclera.
static uint16_t textures[EYE_TEXTURE_POOL];
static int style_index;
static bool pair_layout;             // false: one big eye; true: two side by side
static int brightness_index = BRIGHTNESS_DEFAULT;
static int message_frames;

// Copy an n x n texture into RAM, at full size or (for the small eyes of a
// pair, which would otherwise skip texels and sparkle) averaged down to half.
// Returns the size of the copy.
static int copy_texture(uint16_t *dst, const uint16_t *src, int n, bool half) {
    if (!half || n < 4) {
        memcpy(dst, src, (size_t)n * n * sizeof(uint16_t));
        return n;
    }
    const int h = n / 2;
    for (int y = 0; y < h; y++) {
        const uint16_t *a = src + 2 * y * n, *b = a + n;
        for (int x = 0; x < h; x++) {
            uint32_t p[4] = {a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1]};
            uint32_t r = 0, g = 0, bl = 0;
            for (int i = 0; i < 4; i++) {
                r += p[i] >> 11;
                g += (p[i] >> 5) & 63;
                bl += p[i] & 31;
            }
            dst[y * h + x] = (uint16_t)((((r + 2) >> 2) << 11) | (((g + 2) >> 2) << 5) | ((bl + 2) >> 2));
        }
    }
    return h;
}

// Load a style for the current layout.
static void load_style(int index) {
    style_index = ((index % eye_style_count) + eye_style_count) % eye_style_count;
    const eye_style_t *style = eye_styles[style_index];
    int iris_n = copy_texture(textures, style->iris, style->iris_n, pair_layout);
    uint16_t *sclera = textures + iris_n * iris_n;
    int sclera_n = copy_texture(sclera, style->sclera, style->sclera_n, pair_layout);
    eye_init(&renderer, pair_layout ? PAIR_SIZE : EYE_SIZE, style, textures, iris_n,
             sclera, sclera_n, EYE_FLAG_SWAP | EYE_FLAG_SHADE | EYE_FLAG_HIGHLIGHT);
}

// --- Settings kept in the clock chip's spare byte: style in the low four
// bits, layout in the next, brightness level plus one in the top three. Zero
// means "never set".

static void load_settings(void) {
    uint8_t v = 0;
    int style = 0;
    if (pcf85063_read_ram(&v) && (v >> 5) >= 1 && (v >> 5) <= BRIGHTNESS_COUNT
        && (v & 15) < eye_style_count) {
        style = v & 15;
        pair_layout = (v >> 4) & 1;
        brightness_index = (v >> 5) - 1;
    }
    load_style(style);
}

static void save_settings(void) {
    pcf85063_write_ram((uint8_t)((style_index & 15) | (pair_layout ? 16 : 0)
                                 | ((brightness_index + 1) << 5)));
}

// A short message under the eye. It clears itself.
static void message(uint16_t color, const char *text) {
    eye_screen_text(STATUS_Y, color, "   %s", text);
    message_frames = MESSAGE_FRAMES;
}

// --- Touch gestures.

typedef enum { G_NONE, G_TAP, G_DOUBLE_TAP, G_HOLD, G_LEFT, G_RIGHT, G_UP, G_DOWN } gesture_t;

// Call once a frame. Reports a hold while the finger is still down and a swipe
// when it lifts. A tap is reported a moment after it lifts, once it is clear
// that no second tap is coming.
static gesture_t read_gesture(void) {
    static bool was_touched, hold_reported, tap_waiting;
    static int x0, y0, x, y;
    static float held_s, tap_wait_s;
    gesture_t g = G_NONE;

    bool touched = FT3168_Get_Point() != 0;
    if (touched) {
        x = FT3168.x_point;
        y = FT3168.y_point;
        if (!was_touched) {
            x0 = x;
            y0 = y;
            held_s = 0;
            hold_reported = false;
        }
        held_s += FRAME_S;
    }
    int dx = x - x0, dy = y - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (touched) {
        if (!hold_reported && held_s >= HOLD_S && ax < TAP_PX && ay < TAP_PX) {
            hold_reported = true;
            g = G_HOLD;
        }
    } else if (was_touched && !hold_reported) {
        if (ax >= SWIPE_PX && ax > ay) g = dx < 0 ? G_LEFT : G_RIGHT;
        else if (ay >= SWIPE_PX && ay > ax) g = dy < 0 ? G_UP : G_DOWN;
        else if (ax < TAP_PX && ay < TAP_PX) {
            if (tap_waiting) {
                tap_waiting = false;
                g = G_DOUBLE_TAP;
            } else {
                tap_waiting = true;
                tap_wait_s = 0;
            }
        }
    }
    if (tap_waiting && !touched && (tap_wait_s += FRAME_S) > DOUBLE_TAP_S) {
        tap_waiting = false;
        g = G_TAP;
    }
    was_touched = touched;
    return g;
}

// Read the motion sensor and turn its axes into the screen's: x right, y down,
// z out of the screen toward the viewer.
//
// Found by testing on the board: the sensor's x points to the top of the
// screen, its y to the right, and its z into the board. Rotation rates follow
// from that: turning about the screen's vertical axis is the sensor's x rate,
// and tipping the top of the screen toward the viewer is its y rate.
static bool read_imu(eye_imu_t *imu) {
    float a[3], g[3];
    if (!qmi8658_read_motion(a, g)) return false;
    imu->ax = a[1];
    imu->ay = -a[0];
    imu->az = -a[2];
    imu->gx = -g[1];
    imu->gy = g[0];
    imu->gz = g[2];
    return true;
}

// Blank the screen and turn the board off. On battery this is the end: the
// power chip cuts the supply. On USB the board cannot turn off, so it naps
// with the screen dark until it is moved.
static void go_to_sleep(void) {
    eye_screen_clear(0, SCREEN_H);
    eye_screen_brightness(0);
    printf("still for %.0f s: powering off\n", (double)EYE_SLEEP_AFTER_S);
    sleep_ms(50);
    pwr_button_power_off();
    sleep_ms(500);

    eye_imu_t imu;
    do {
        sleep_ms(100);
    } while (!read_imu(&imu) || !eye_motion_is_moving(&motion, &imu));
    eye_motion_init(&motion);
    eye_screen_brightness(brightness_levels[brightness_index]);
}

int main(void) {
    stdio_init_all();
    eye_screen_init(brightness_levels[BRIGHTNESS_DEFAULT]);
    bool imu_ok = qmi8658_init_motion();

    load_settings();
    eye_screen_brightness(brightness_levels[brightness_index]);
    eye_behave_init(&behave, get_rand_32());   // a different sequence every power-on
    eye_motion_init(&motion);

    bool show_stats = false, unlocked = false;
    float idle_s = 0, battery_s = BATTERY_EVERY_S, warn_s = BATTERY_WARN_EVERY_S;
    int battery = 0;
    uint32_t frames = 0, busy_us = 0;
    eye_imu_t imu = {0};
    eye_reaction_t react = {0};
    absolute_time_t last = get_absolute_time(), window_start = last;
    absolute_time_t next_frame = delayed_by_us(last, FRAME_US);

    while (true) {
        absolute_time_t now = get_absolute_time();
        float dt = absolute_time_diff_us(last, now) / 1e6f;
        last = now;

        if (imu_ok && read_imu(&imu)) {
            eye_motion_update(&motion, &imu, dt, &react);
            if (react.sleep) {
                go_to_sleep();
                react = (eye_reaction_t){0};
                unlocked = false;
                message_frames = 0;
                last = get_absolute_time();
                next_frame = delayed_by_us(last, FRAME_US);
                window_start = last;
                frames = 0;
                busy_us = 0;
                continue;
            }
        }

        eye_pose_t pose;
        eye_behave_update(&behave, &renderer, &react, dt, &pose);
        if (pair_layout) {
            eye_screen_draw_pair(&renderer, &pose);
        } else {
            eye_screen_draw(&renderer, &pose);
        }
        frames++;

        // Touch. Locked, only a hold does anything.
        gesture_t g = read_gesture();
        if (g != G_NONE) idle_s = 0;
        if (!unlocked) {
            if (g == G_HOLD) {
                unlocked = true;
                message(GREEN, "UNLOCKED");
            }
        } else if (g == G_HOLD || (idle_s += dt) > RELOCK_S) {
            unlocked = false;
            message(WHITE, "LOCKED");
        } else if (g == G_LEFT || g == G_RIGHT) {
            load_style(style_index + (g == G_LEFT ? 1 : -1));
            save_settings();
            message(WHITE, eye_styles[style_index]->name);
        } else if (g == G_UP || g == G_DOWN) {
            brightness_index += g == G_UP ? 1 : -1;
            if (brightness_index < 0) brightness_index = 0;
            if (brightness_index >= BRIGHTNESS_COUNT) brightness_index = BRIGHTNESS_COUNT - 1;
            eye_screen_brightness(brightness_levels[brightness_index]);
            save_settings();
            char text[20];
            snprintf(text, sizeof(text), "BRIGHTNESS %d/%d", brightness_index + 1, BRIGHTNESS_COUNT);
            message(WHITE, text);
        } else if (g == G_TAP) {
            pair_layout = !pair_layout;
            eye_screen_clear(0, SCREEN_H);               // the old eye is a different shape
            load_style(style_index);
            save_settings();
            message_frames = 0;
        } else if (g == G_DOUBLE_TAP) {
            show_stats = !show_stats;
            if (!show_stats) {
                eye_screen_clear(TOP_Y, TOP_Y + TEXT_H);
                eye_screen_clear(STATUS_Y, STATUS_Y + TEXT_H);
                message_frames = 0;
            }
        }
        if (message_frames > 0 && --message_frames == 0) {
            eye_screen_clear(STATUS_Y, STATUS_Y + TEXT_H);
        }

        // Battery. A reading of 0 means no battery is attached (or an empty
        // one, in which case the board is about to stop anyway).
        battery_s += dt;
        warn_s += dt;
        if (battery_s >= BATTERY_EVERY_S) {
            battery_s = 0;
            battery = DEV_I2C_ReadByte(AXP2101_ADDR, AXP2101_REG_BATTERY_PERCENT);
        }
        if (battery > 0 && battery <= BATTERY_LOW_PERCENT && warn_s >= BATTERY_WARN_EVERY_S
            && message_frames == 0) {
            warn_s = 0;
            message(RED, "LOW BATTERY");
        }

        busy_us += (uint32_t)absolute_time_diff_us(now, get_absolute_time());
        if (frames >= STATS_EVERY) {
            float secs = absolute_time_diff_us(window_start, get_absolute_time()) / 1e6f;
            float fps = frames / secs;
            float busy_ms = busy_us / 1000.0f / frames;
            char state = react.shake > 0 ? 'S' : react.dizzy > 0 ? 'Z' : react.drowsy > 0 ? 'D' : ' ';
            if (show_stats) {
                if (imu_ok) {
                    eye_screen_text(TOP_Y, YELLOW, "    %+4.1f%+5.1f%+5.1f %c %3d%%",
                                    imu.ax, imu.ay, imu.az, state, battery);
                } else {
                    eye_screen_text(TOP_Y, RED, "    NO MOTION SENSOR");
                }
                if (message_frames == 0) {
                    eye_screen_text(STATUS_Y, YELLOW, "   %4.1f FPS  BUSY %2.0f MS", fps, busy_ms);
                }
            }
            printf("%s  %.1f fps, busy %.1f ms  accel %+.2f %+.2f %+.2f  %c  battery %d%%  %s\n",
                   eye_styles[style_index]->name, fps, busy_ms, imu.ax, imu.ay, imu.az, state,
                   battery, unlocked ? "unlocked" : "locked");
            frames = 0;
            busy_us = 0;
            window_start = get_absolute_time();
        }

        // Wait out the rest of the frame. If a frame ran long, start counting
        // again from now instead of trying to catch up.
        if (absolute_time_diff_us(get_absolute_time(), next_frame) < 0) {
            next_frame = get_absolute_time();
        } else {
            sleep_until(next_frame);
        }
        next_frame = delayed_by_us(next_frame, FRAME_US);
    }
}
