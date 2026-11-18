// Render benchmark: one full-size textured eye, running flat out, with a
// frame-rate readout.
//
// The eye looks around, changes pupil size and blinks on a fixed loop, so the
// measured frame rate covers every part of the renderer. Tap the screen to
// switch between FULL (edge shading and highlight) and PLAIN (neither).
//
// The bottom line shows frames per second and the milliseconds each frame took
// to draw and send. The same line prints over USB serial.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "FT3168.h"
#include "GUI_Paint.h"
#include "eye.h"
#include "eye_screen.h"

#define STATS_EVERY 20               // frames between readout updates

static eye_renderer_t renderer;
// Textures are copied to RAM: reading them from flash per pixel is slower.
static uint16_t iris_ram[192 * 192];
static uint16_t sclera_ram[256 * 256];

// The fixed animation loop. t is seconds since start.
static eye_pose_t pose_at(float t) {
    eye_pose_t p;
    p.gaze_x = 0.8f * sinf(0.9f * t);
    p.gaze_y = 0.5f * sinf(1.37f * t + 1.0f);
    p.pupil = 0.5f + 0.45f * sinf(0.5f * t);
    float phase = fmodf(t, 4.0f);                       // a blink every 4 seconds
    float shut = phase < 0.3f ? 1.0f - fabsf(phase / 0.15f - 1.0f) : 0.0f;
    // The upper lid follows the gaze down, the lower lid follows it up.
    p.lid_upper = (0.74f - 0.22f * p.gaze_y) * (1.0f - shut);
    p.lid_lower = (0.74f + 0.12f * p.gaze_y) * (1.0f - shut);
    return p;
}

int main(void) {
    stdio_init_all();
    eye_screen_init(60);

    const eye_style_t *style = eye_styles[0];   // hazel: iris 192, sclera 256
    memcpy(iris_ram, style->iris, sizeof(iris_ram));
    memcpy(sclera_ram, style->sclera, sizeof(sclera_ram));
    const unsigned full = EYE_FLAG_SWAP | EYE_FLAG_SHADE | EYE_FLAG_HIGHLIGHT;
    const unsigned plain = EYE_FLAG_SWAP;
    eye_init(&renderer, EYE_SIZE, style, iris_ram, style->iris_n, sclera_ram, style->sclera_n, full);

    bool is_full = true, was_touched = false;
    uint32_t frames = 0;
    absolute_time_t start = get_absolute_time(), window_start = start;

    while (true) {
        float t = absolute_time_diff_us(start, get_absolute_time()) / 1e6f;
        eye_pose_t pose = pose_at(t);
        eye_screen_draw(&renderer, &pose);
        frames++;

        if ((frames & 3) == 0) {                         // a tap switches mode
            bool touched = FT3168_Get_Point() != 0;
            if (touched && !was_touched) {
                is_full = !is_full;
                renderer.flags = is_full ? full : plain;
            }
            was_touched = touched;
        }

        if (frames == STATS_EVERY) {
            float secs = absolute_time_diff_us(window_start, get_absolute_time()) / 1e6f;
            float fps = frames / secs;
            float frame_ms = secs * 1000.0f / frames;
            eye_screen_text(STATUS_Y, YELLOW, "  %s %4.1f FPS  %2.0f MS",
                            is_full ? "FULL " : "PLAIN", fps, frame_ms);
            printf("%s %.1f fps, %.1f ms per frame\n", is_full ? "full" : "plain", fps, frame_ms);
            frames = 0;
            window_start = get_absolute_time();
        }
    }
}
