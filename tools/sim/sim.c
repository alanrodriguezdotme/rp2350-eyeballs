// Renders the eye on a computer and writes PPM images, to check the renderer
// without flashing a board. Build and run with tools/sim/run.sh.
//
//   sim <dir> <size>               fixed test poses of the first style
//   sim <dir> <size> anim <n>      n frames of the idle behaviour at 30fps
//   sim <dir> <size> styles        every style, after one second of idling
//   sim <dir> <size> pairs         every style as a pair of eyes, each <size> wide
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "behave.h"
#include "eye.h"

static eye_renderer_t renderer;
static uint16_t frame[EYE_MAX_SIZE * EYE_MAX_SIZE];

static void save(const char *path, int size) {
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", size, size);
    for (int i = 0; i < size * size; i++) {
        uint16_t c = frame[i];
        unsigned char rgb[3] = {(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    int size = argc > 2 ? atoi(argv[2]) : 368;
    static const struct { const char *name; eye_pose_t pose; } shots[] = {
        {"centre",      {0, 0, 0.5f, 0.74f, 0.74f, 0}},
        {"right",       {1, 0, 0.5f, 0.74f, 0.74f, 0}},
        {"up_left",     {-0.8f, -0.8f, 0.5f, 0.92f, 0.64f, 0}},
        {"down",        {0, 1, 0.5f, 0.52f, 0.86f, 0}},
        {"pupil_small", {0, 0, 0, 1, 1, 0}},
        {"pupil_large", {0, 0, 1, 1, 1, 0}},
        {"half_blink",  {0.3f, 0.1f, 0.5f, 0.5f, 0.5f, 0}},
        {"closed",      {0, 0, 0.5f, 0, 0, 0}},
    };
    const unsigned flags = EYE_FLAG_SHADE | EYE_FLAG_HIGHLIGHT;
    eye_init(&renderer, size, eye_styles[0], NULL, 0, NULL, 0, flags);
    if (argc > 3 && argv[3][0] == 'p') {
        static uint16_t wide[2 * EYE_MAX_SIZE * EYE_MAX_SIZE];
        static eye_frame_t left, right;
        for (int k = 0; k < eye_style_count; k++) {
            static eye_behave_t behave;
            char path[256];
            eye_pose_t pose;
            eye_init(&renderer, size, eye_styles[k], NULL, 0, NULL, 0, flags);
            eye_behave_init(&behave, 4242);
            behave.blink_wait = 100;
            for (int i = 0; i < 30; i++) eye_behave_update(&behave, &renderer, NULL, 1.0f / 30.0f, &pose);
            eye_frame_setup(&renderer, &pose, false, &left);
            eye_frame_setup(&renderer, &pose, true, &right);
            eye_frame_render(&renderer, &left, 0, size, wide, 2 * size);
            eye_frame_render(&renderer, &right, 0, size, wide + size, 2 * size);
            snprintf(path, sizeof(path), "%s/pair%02d_%s.ppm", dir, k, eye_styles[k]->name);
            FILE *f = fopen(path, "wb");
            fprintf(f, "P6\n%d %d\n255\n", 2 * size, size);
            for (int i = 0; i < 2 * size * size; i++) {
                uint16_t c = wide[i];
                unsigned char rgb[3] = {(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
        return 0;
    }
    if (argc > 3 && argv[3][0] == 's') {
        for (int k = 0; k < eye_style_count; k++) {
            static eye_behave_t behave;
            char path[256];
            eye_pose_t pose;
            eye_init(&renderer, size, eye_styles[k], NULL, 0, NULL, 0, flags);
            eye_behave_init(&behave, 4242);
            behave.blink_wait = 100;
            for (int i = 0; i < 30; i++) eye_behave_update(&behave, &renderer, NULL, 1.0f / 30.0f, &pose);
            eye_begin_frame(&renderer, &pose);
            eye_render_rows(&renderer, 0, size, frame);
            snprintf(path, sizeof(path), "%s/style%02d_%s.ppm", dir, k, eye_styles[k]->name);
            save(path, size);
        }
        return 0;
    }
    if (argc > 4 && argv[3][0] == 'a') {
        static eye_behave_t behave;
        eye_behave_init(&behave, 12345);
        int n = atoi(argv[4]);
        for (int i = 0; i < n; i++) {
            char path[256];
            eye_pose_t pose;
            eye_behave_update(&behave, &renderer, NULL, 1.0f / 30.0f, &pose);
            eye_begin_frame(&renderer, &pose);
            eye_render_rows(&renderer, 0, size, frame);
            snprintf(path, sizeof(path), "%s/anim%04d.ppm", dir, i);
            save(path, size);
        }
        return 0;
    }
    for (unsigned i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        char path[256];
        eye_begin_frame(&renderer, &shots[i].pose);
        eye_render_rows(&renderer, 0, size, frame);
        snprintf(path, sizeof(path), "%s/%s.ppm", dir, shots[i].name);
        save(path, size);
    }
    clock_t t0 = clock();
    for (int i = 0; i < 200; i++) {
        eye_pose_t p = {(i % 20) / 10.0f - 1, 0, 0.5f, 1, 1, 0};
        eye_begin_frame(&renderer, &p);
        eye_render_rows(&renderer, 0, size, frame);
    }
    printf("%.2f ms per frame on this computer\n",
           (double)(clock() - t0) / CLOCKS_PER_SEC * 1000 / 200);
    return 0;
}
