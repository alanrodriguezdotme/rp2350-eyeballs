// Milestone 1 bring-up for the Waveshare RP2350-Touch-AMOLED-1.8.
//
// What it does, in order:
//   1. Red, green, blue, white full-screen fills (panel and colour order).
//   2. Fill-rate benchmark: full screen, then a 368x368 eye-sized window.
//   3. Two test squares: one on even coordinates, one on odd coordinates.
//   4. Live status: accelerometer, touch, PWR button, battery.
//   5. Hold a finger on the screen for 3 seconds to test software power-off.
//
// Everything on screen is also printed over USB serial.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "AMOLED_1in8.h"
#include "DEV_Config.h"
#include "FT3168.h"
#include "GUI_Paint.h"
#include "pwr_button.h"
#include "qmi8658.h"
#include "qspi_pio.h"

#define W AMOLED_1IN8_WIDTH   // 368
#define H AMOLED_1IN8_HEIGHT  // 448
#define BAND_H 24             // one line of Font20 text
#define STRIP_ROWS 16
#define BENCH_FRAMES 30

#define AXP2101_ADDR 0x34
#define AXP2101_REG_BATTERY_PERCENT 0xA4  // from the AXP2101 register map; unverified on this board

// Pixels go out high byte first, so buffers hold big-endian RGB565.
static uint8_t band[W * BAND_H * 2];
static uint8_t strip_a[W * STRIP_ROWS * 2];
static uint8_t strip_b[W * STRIP_ROWS * 2];

static void strip_fill(uint8_t *strip, uint16_t color) {
    for (uint32_t i = 0; i < W * STRIP_ROWS; i++) {
        strip[2 * i] = color >> 8;
        strip[2 * i + 1] = color & 0xff;
    }
}

// Stream a prefilled strip into a window. Returns bytes sent.
static uint32_t stream_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                              const uint8_t *strip) {
    uint32_t w = x1 - x0, rows = y1 - y0;
    uint32_t rows_per_chunk = (W * STRIP_ROWS) / w;
    uint32_t sent = 0;
    AMOLED_1IN8_DisplayStreamBegin(x0, y0, x1, y1);
    while (rows) {
        uint32_t n = rows < rows_per_chunk ? rows : rows_per_chunk;
        AMOLED_1IN8_DisplayStreamWrite(strip, n * w * 2);
        sent += n * w * 2;
        rows -= n;
    }
    AMOLED_1IN8_DisplayStreamEnd();
    return sent;
}

static void fill(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint16_t color) {
    strip_fill(strip_a, color);
    stream_window(x0, y0, x1, y1, strip_a);
}

// Draw one line of text at pixel row y (max 26 characters) and echo it to USB.
static void text_line(uint32_t y, uint16_t color, const char *fmt, ...) {
    char buf[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[26] = 0;
    Paint_SelectImage(band);
    memset(band, 0, sizeof(band));
    Paint_DrawString_EN(2, 2, buf, &Font20, color, BLACK);
    AMOLED_1IN8_DisplayWindowPacked(0, y, W, y + BAND_H, (UWORD *)band);
}

// Time BENCH_FRAMES fills of a window, alternating two colours so it is visible.
static void bench(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                  float *fps, float *mb_per_s) {
    strip_fill(strip_a, 0x18E3);  // dark grey
    strip_fill(strip_b, 0x4208);  // mid grey
    uint32_t bytes = 0;
    absolute_time_t t0 = get_absolute_time();
    for (int f = 0; f < BENCH_FRAMES; f++) {
        bytes += stream_window(x0, y0, x1, y1, (f & 1) ? strip_b : strip_a);
    }
    float secs = absolute_time_diff_us(t0, get_absolute_time()) / 1e6f;
    *fps = BENCH_FRAMES / secs;
    *mb_per_s = bytes / secs / 1e6f;
}

int main(void) {
    stdio_init_all();
    DEV_Module_Init();  // I2C, DMA channel, speaker amp held off
    QSPI_GPIO_Init(qspi);
    QSPI_PIO_Init(qspi);
    QSPI_4Wrie_Mode(&qspi);
    AMOLED_1IN8_Init();
    AMOLED_1IN8_SetBrightness(60);

    Paint_NewImage(band, W, BAND_H, 0, BLACK);
    Paint_SetScale(65);

    // 1. Colour fills.
    static const uint16_t colors[] = {RED, GREEN, BLUE, WHITE};
    for (unsigned i = 0; i < 4; i++) {
        fill(0, 0, W, H, colors[i]);
        sleep_ms(400);
    }

    // 2. Fill-rate benchmark.
    float fps_full, mbs_full, fps_eye, mbs_eye;
    bench(0, 0, W, H, &fps_full, &mbs_full);
    bench(0, 40, W, 40 + 368, &fps_eye, &mbs_eye);
    printf("full screen: %.1f fps, %.1f MB/s\n", fps_full, mbs_full);
    printf("368x368:     %.1f fps, %.1f MB/s\n", fps_eye, mbs_eye);

    // 3. Static part of the status screen, including the alignment squares.
    fill(0, 0, W, H, BLACK);
    text_line(0, YELLOW, "EYEBALLS BRING-UP");
    text_line(24, WHITE, "FULL   %5.1f FPS %4.1f MB/S", fps_full, mbs_full);
    text_line(48, WHITE, "EYE368 %5.1f FPS %4.1f MB/S", fps_eye, mbs_eye);
    fill(40, 80, 140, 180, GREEN);     // even start, even size (100x100)
    fill(201, 81, 300, 180, MAGENTA);  // odd start, odd size (99x99)
    text_line(188, WHITE, "GREEN=EVEN  PINK=ODD");

    bool imu_ok = qmi8658_init_accelerometer();
    FT3168_Init(FT3168_Point_Mode);
    uint16_t touch_id = FT3168_ReadID();
    pwr_button_enable_edges();
    text_line(220, imu_ok ? GREEN : RED, "IMU %s  TOUCH ID %04X",
              imu_ok ? "OK" : "FAIL", touch_id);
    text_line(412, YELLOW, "HOLD TOUCH 3S = POWER OFF");
    printf("imu %s, touch id %04x\n", imu_ok ? "ok" : "FAIL", touch_id);

    // 4. Live status.
    uint32_t press_count = 0, long_count = 0;
    absolute_time_t touch_since = nil_time;
    uint32_t tick = 0;
    while (true) {
        qmi8658_accel_raw_t a = {0};
        bool got = imu_ok && qmi8658_read_accel(&a);
        text_line(252, got ? WHITE : RED, "ACC %6d %6d %6d", a.x, a.y, a.z);

        uint8_t fingers = FT3168_Get_Point();
        if (fingers) {
            text_line(276, WHITE, "TOUCH %d  X%4d Y%4d", fingers,
                      FT3168.x_point, FT3168.y_point);
        } else {
            text_line(276, WHITE, "TOUCH -");
        }

        pwr_button_event_t ev = pwr_button_poll();
        if (ev & PWR_BUTTON_SHORT_PRESS) press_count++;
        if (ev & PWR_BUTTON_LONG_PRESS) long_count++;
        text_line(300, WHITE, "PWR BTN SHORT %lu LONG %lu",
                  (unsigned long)press_count, (unsigned long)long_count);

        if ((tick % 10) == 0) {
            uint8_t pct = DEV_I2C_ReadByte(AXP2101_ADDR, AXP2101_REG_BATTERY_PERCENT);
            text_line(324, WHITE, "BATTERY %3d %%", pct);
            printf("acc %d %d %d  touch %d (%d,%d)  batt %d%%\n", a.x, a.y, a.z,
                   fingers, FT3168.x_point, FT3168.y_point, pct);
        }

        // 5. Software power-off test.
        if (fingers) {
            if (is_nil_time(touch_since)) touch_since = get_absolute_time();
            if (absolute_time_diff_us(touch_since, get_absolute_time()) > 3000000) {
                text_line(412, RED, "POWERING OFF...");
                sleep_ms(800);
                pwr_button_power_off();
                sleep_ms(1000);
                // Still running: the PMIC did not cut power (likely on USB).
                text_line(412, RED, "STILL ON (USB POWER?)");
                touch_since = nil_time;
                sleep_ms(1500);
                text_line(412, YELLOW, "HOLD TOUCH 3S = POWER OFF");
            }
        } else {
            touch_since = nil_time;
        }

        tick++;
        sleep_ms(100);
    }
}
