#include "eye_screen.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "AMOLED_1in8.h"
#include "DEV_Config.h"
#include "FT3168.h"
#include "GUI_Paint.h"
#include "qspi_pio.h"

#define STRIP_ROWS 16

// Four strips: each core draws one while the previous pair is on its way to
// the panel.
static uint16_t strips[4][EYE_SIZE * STRIP_ROWS];
static uint8_t band[SCREEN_W * TEXT_H * 2];
// What the current frame is made of. Set before core 1 is given any work.
static const eye_renderer_t *active;
static bool active_pair;
static eye_frame_t pair_frames[2];

// Draw rows y .. y+rows-1 of the current frame into buf, a full-width strip.
static void render_strip(int y, int rows, uint16_t *buf) {
    if (active_pair) {
        eye_frame_render(active, &pair_frames[0], y, rows, buf, SCREEN_W);
        eye_frame_render(active, &pair_frames[1], y, rows, buf + PAIR_SIZE, SCREEN_W);
    } else {
        eye_render_rows(active, y, rows, buf);
    }
}

static int strip_rows(int y) {
    int left = active->size - y;
    return left < STRIP_ROWS ? left : STRIP_ROWS;
}

// Core 1 waits for a job (which strip to draw, into which buffer), draws it,
// and sends the job back to say it is done.
static void core1_main(void) {
    while (true) {
        uint32_t job = multicore_fifo_pop_blocking();
        int y = (int)(job >> 8);
        render_strip(y, strip_rows(y), strips[job & 0xff]);
        multicore_fifo_push_blocking(job);
    }
}

void eye_screen_clear(uint32_t y0, uint32_t y1) {
    memset(strips[0], 0, sizeof(strips[0]));
    AMOLED_1IN8_DisplayStreamBegin(0, y0, SCREEN_W, y1);
    for (uint32_t y = y0; y < y1; y += STRIP_ROWS) {
        uint32_t n = y1 - y < STRIP_ROWS ? y1 - y : STRIP_ROWS;
        AMOLED_1IN8_DisplayStreamWrite(strips[0], n * SCREEN_W * 2);
    }
    AMOLED_1IN8_DisplayStreamEnd();
}

void eye_screen_init(uint8_t brightness) {
    DEV_Module_Init();   // I2C, DMA channel, speaker amp held off
    QSPI_GPIO_Init(qspi);
    QSPI_PIO_Init(qspi);
    QSPI_4Wrie_Mode(&qspi);
    AMOLED_1IN8_Init();
    AMOLED_1IN8_SetBrightness(brightness);
    Paint_NewImage(band, SCREEN_W, TEXT_H, 0, BLACK);
    Paint_SetScale(65);
    FT3168_Init(FT3168_Point_Mode);
    eye_screen_clear(0, SCREEN_H);
    multicore_launch_core1(core1_main);
}

void eye_screen_brightness(uint8_t brightness) { AMOLED_1IN8_SetBrightness(brightness); }

// Strips are drawn two at a time, one per core, and sent in order. Each pair
// uses the buffers the previous pair is not using, so a strip still on its way
// to the panel is never overwritten.
static void draw_and_send(int top) {
    const int height = active->size;
    AMOLED_1IN8_DisplayStreamBegin(0, top, SCREEN_W, top + height);
    for (int y = 0, set = 0; y < height; y += 2 * STRIP_ROWS, set ^= 2) {
        uint16_t *first = strips[set], *second = strips[set + 1];
        bool two = y + STRIP_ROWS < height;
        if (two) multicore_fifo_push_blocking(((uint32_t)(y + STRIP_ROWS) << 8) | (uint32_t)(set + 1));
        render_strip(y, strip_rows(y), first);
        AMOLED_1IN8_DisplayStreamWait();                 // previous strip has left
        AMOLED_1IN8_DisplayStreamWriteAsync(first, (uint32_t)strip_rows(y) * SCREEN_W * 2);
        if (two) {
            (void)multicore_fifo_pop_blocking();         // core 1 has finished
            AMOLED_1IN8_DisplayStreamWait();
            AMOLED_1IN8_DisplayStreamWriteAsync(
                second, (uint32_t)strip_rows(y + STRIP_ROWS) * SCREEN_W * 2);
        }
    }
    AMOLED_1IN8_DisplayStreamWait();
    AMOLED_1IN8_DisplayStreamEnd();
}

void eye_screen_draw(eye_renderer_t *renderer, const eye_pose_t *pose) {
    eye_begin_frame(renderer, pose);
    active = renderer;
    active_pair = false;
    draw_and_send(EYE_Y);
}

void eye_screen_draw_pair(eye_renderer_t *renderer, const eye_pose_t *pose) {
    eye_frame_setup(renderer, pose, false, &pair_frames[0]);
    eye_frame_setup(renderer, pose, true, &pair_frames[1]);
    active = renderer;
    active_pair = true;
    draw_and_send(PAIR_Y);
}

void eye_screen_text(uint32_t y, uint16_t color, const char *fmt, ...) {
    char buf[40];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[26] = 0;
    memset(band, 0, sizeof(band));
    Paint_SelectImage(band);
    Paint_DrawString_EN(2, 2, buf, &Font20, color, BLACK);
    AMOLED_1IN8_DisplayWindowPacked(0, y, SCREEN_W, y + TEXT_H, (UWORD *)band);
}
