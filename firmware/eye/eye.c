#include "eye.h"

#include <math.h>
#include <string.h>

#if PICO_ON_DEVICE
#include "pico.h"
#define EYE_HOT(f) __not_in_flash_func(f)   // keep the inner loop in RAM
#else
#define EYE_HOT(f) f
#endif

#define LUT_SHIFT 16          // radius^2 (Q12 px^2) -> LUT index
#define LUT_IRIS 0x8000       // LUT flag: sample the iris texture
#define GAZE_MAX_RAD 0.45f    // gaze of +/-1 turns the eye this far
#define Z_OUTSIDE 0xFFFF

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Radius in pixels at the top edge of LUT bin i.
static float bin_radius(int i) {
    return sqrtf((float)(i + 1) * (float)(1 << LUT_SHIFT)) / 64.0f;
}

static uint16_t lut_scale(float tex_r, float rho) {
    float s = tex_r / rho * 2048.0f;   // Q11
    if (s > 32767.0f) s = 32767.0f;
    if (s < 0.0f) s = 0.0f;
    return (uint16_t)s;
}

// Sclera bins never change: texture radius is proportional to the angle from
// the pupil axis.
static void build_sclera_lut(eye_renderer_t *r) {
    const float tex_max = (float)(r->sclera_n / 2 - 1);
    for (int i = 0; i < EYE_LUT_N; i++) {
        float rho = bin_radius(i);
        float x = rho / r->radius_px;
        float phi = x >= 1.0f ? (float)M_PI_2 : asinf(x);
        r->lut[i] = lut_scale(tex_max * phi / (float)M_PI_2, rho);
    }
}

// Iris bins depend on the pupil size: the iris texture stretches between the
// pupil edge and the iris edge. Bins inside the pupil get scale 0, which lands
// on the pupil-coloured centre of the texture.
static void build_iris_lut(eye_renderer_t *r, float pupil) {
    const eye_style_t *st = r->style;
    const float tex_max = (float)(r->iris_n / 2 - 1);
    float frac = st->pupil_min + (st->pupil_max - st->pupil_min) * clampf(pupil, 0, 1);
    float pupil_px = frac * r->iris_px;
    r->slit_xc = r->slit_rc2 = 0;
    if (st->slit_radius > 0) {
        // A slit pupil is the overlap of two circles, one centred off to each
        // side: a tall lens, slit_px high and pupil_px wide at the middle. It
        // is drawn over the iris, so the iris texture gets no round pupil.
        float h = fminf(st->slit_radius * (float)r->size, r->iris_px * 0.98f);
        float w = clampf(pupil_px, 1.5f, h);
        float xc = fminf((h * h - w * w) / (2.0f * w), 3000.0f);
        float rc = w + xc;
        r->slit_xc = (int32_t)(xc * 4.0f);
        r->slit_rc2 = (int32_t)(rc * 4.0f) * (int32_t)(rc * 4.0f);
        pupil_px = 0;
    }
    for (int i = 0; i < EYE_LUT_N; i++) {
        float rho = bin_radius(i);
        if (rho > r->iris_px) break;
        if (rho <= pupil_px) {
            r->lut[i] = LUT_IRIS;
        } else {
            float t = (rho - pupil_px) / (r->iris_px - pupil_px);
            float tex_r = tex_max * (st->iris_hole + t * (1.0f - st->iris_hole));
            r->lut[i] = LUT_IRIS | lut_scale(tex_r, rho);
        }
    }
    r->last_pupil = pupil;
}

void eye_init(eye_renderer_t *r, int size, const eye_style_t *style,
              const uint16_t *iris, int iris_n, const uint16_t *sclera, int sclera_n,
              unsigned flags) {
    if (size > EYE_MAX_SIZE) size = EYE_MAX_SIZE;
    size &= ~1;
    r->size = size;
    r->half = size / 2;
    r->flags = flags;
    r->style = style;
    r->iris = iris ? iris : style->iris;
    r->iris_n = iris ? iris_n : style->iris_n;
    r->sclera = sclera ? sclera : style->sclera;
    r->sclera_n = sclera ? sclera_n : style->sclera_n;
    r->radius_px = clampf(style->eye_radius, 0.3f, 0.65f) * (float)size;
    r->iris_px = style->iris_radius * (float)size;

    // Height of the sphere above each pixel of one quadrant.
    const float r2 = r->radius_px * r->radius_px;
    for (int y = 0; y < r->half; y++) {
        for (int x = 0; x < r->half; x++) {
            float dx = (float)x + 0.5f, dy = (float)y + 0.5f;
            float d2 = dx * dx + dy * dy;
            r->ztab[y * r->half + x] =
                d2 >= r2 ? Z_OUTSIDE : (uint16_t)(sqrtf(r2 - d2) * 256.0f);
        }
        int w = 0;
        while (w < r->half && r->ztab[y * r->half + w] != Z_OUTSIDE) w++;
        r->zwidth[y] = (uint16_t)w;
    }

    // Edge darkening, indexed by sphere height in pixels. 64 = full brightness.
    for (int i = 0; i < 256; i++) {
        float h = clampf((float)i / r->radius_px, 0.0f, 1.0f);
        r->shade[i] = (uint8_t)(64.0f * (0.30f + 0.70f * powf(h, 0.55f)) + 0.5f);
    }

    // Soft highlight, up and to the left of centre.
    int n = (int)(0.17f * (float)size) & ~1;
    if (n > EYE_HL_MAX) n = EYE_HL_MAX;
    r->hl_n = n;
    r->hl_x = r->half - (int)(0.21f * (float)size) - n / 2;
    r->hl_y = r->half - (int)(0.23f * (float)size) - n / 2;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float dx = ((float)x + 0.5f) / (float)n * 2.0f - 1.0f;
            float dy = ((float)y + 0.5f) / (float)n * 2.0f - 1.0f;
            float d = sqrtf(dx * dx + dy * dy * 1.6f);
            float a = clampf(1.0f - d, 0.0f, 1.0f);
            r->hl[y * n + x] = (uint8_t)(a * a * 30.0f);   // 0..30 of 32
        }
    }

    build_sclera_lut(r);
    build_iris_lut(r, 0.5f);

    eye_pose_t rest = {0, 0, 0.5f, 1, 1, 0};
    eye_begin_frame(r, &rest);
}

static int lid_row(const eye_renderer_t *r, const uint8_t *table, int column) {
    const eye_style_t *st = r->style;
    int c = column * st->lid_n / r->size;
    return table[c] * (r->size - 1) / (st->lid_h - 1);
}

void eye_frame_setup(eye_renderer_t *r, const eye_pose_t *pose, bool mirror_lids,
                     eye_frame_t *frame) {
    // Gaze rotation. Rows are the eye's own x, y and z axes in view space
    // (x right, y down, z toward the viewer).
    float yaw = clampf(pose->gaze_x, -1, 1) * GAZE_MAX_RAD;
    float pitch = clampf(pose->gaze_y, -1, 1) * GAZE_MAX_RAD;
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch);
    const float q = 16384.0f;
    const float ex[3] = {cy, 0, -sy};
    const float ey[3] = {-sy * sp, cp, -cy * sp};
    const float ez[3] = {sy * cp, sp, cy * cp};
    // Spin turns the eye's x and y axes about its z axis (the pupil).
    const float cs = cosf(pose->spin), sn = sinf(pose->spin);
    frame->spin_cos = (int32_t)(cs * q);
    frame->spin_sin = (int32_t)(sn * q);
    for (int i = 0; i < 3; i++) {
        frame->m[i] = (int32_t)((cs * ex[i] + sn * ey[i]) * q);
        frame->m[3 + i] = (int32_t)((cs * ey[i] - sn * ex[i]) * q);
        frame->m[6 + i] = (int32_t)(ez[i] * q);
    }

    if (pose->pupil != r->last_pupil) build_iris_lut(r, pose->pupil);

    const eye_style_t *st = r->style;
    float fu = clampf(pose->lid_upper, 0, 1), fl = clampf(pose->lid_lower, 0, 1);
    for (int x = 0; x < r->size; x++) {
        const int col = mirror_lids ? r->size - 1 - x : x;
        int uo = lid_row(r, st->upper_open, col), uc = lid_row(r, st->upper_closed, col);
        int lo = lid_row(r, st->lower_open, col), lc = lid_row(r, st->lower_closed, col);
        int top = (int)((float)uc + fu * (float)(uo - uc) + 0.5f);
        int bot = (int)((float)lc + fl * (float)(lo - lc) + 0.5f);
        if (top < 0) top = 0;
        if (bot > r->size - 1) bot = r->size - 1;
        // Packed so the row loop needs one load and one compare per pixel:
        // a pixel is open when (unsigned)(y - top) <= bot - top.
        frame->lid[x] = bot < top ? 0x7FFFu : ((uint32_t)top | ((uint32_t)(bot - top) << 16));
    }
}

void eye_begin_frame(eye_renderer_t *r, const eye_pose_t *pose) {
    eye_frame_setup(r, pose, false, &r->frame);
}

void eye_track_lids(const eye_renderer_t *r, float gaze_x, float gaze_y,
                    float *upper, float *lower) {
    const eye_style_t *st = r->style;
    float yaw = clampf(gaze_x, -1, 1) * GAZE_MAX_RAD;
    float pitch = clampf(gaze_y, -1, 1) * GAZE_MAX_RAD;
    // Where the pupil lands on screen.
    float cx = (float)r->half + r->radius_px * sinf(yaw) * cosf(pitch);
    float cy = (float)r->half + r->radius_px * sinf(pitch);
    int col = (int)clampf(cx, 0, (float)(r->size - 1));
    float uo = (float)lid_row(r, st->upper_open, col), uc = (float)lid_row(r, st->upper_closed, col);
    float lo = (float)lid_row(r, st->lower_open, col), lc = (float)lid_row(r, st->lower_closed, col);
    // Wide open, so the eye fills the screen: both lids clear the iris.
    float want_top = cy - r->iris_px * 1.30f;
    float want_bot = cy + r->iris_px * 1.55f;
    *upper = uo == uc ? 1.0f : clampf((want_top - uc) / (uo - uc), 0, 1);
    *lower = lo == lc ? 1.0f : clampf((want_bot - lc) / (lo - lc), 0, 1);
}

// Ordered dither, 0..60. Added before the colour is cut back to 5 or 6 bits,
// it turns the shading's contour bands into a pattern too fine to see.
// Not const on purpose: that keeps it in RAM, next to the loop that reads it.
static uint8_t dither4[16] = {0, 32, 8, 40, 48, 16, 56, 24, 12, 44, 4, 36, 60, 28, 52, 20};

// Scale an RGB565 colour by f/64 (f: 0..64), dithered by d (0..63).
static inline uint16_t scale565(uint32_t c, uint32_t f, uint32_t d) {
    return (uint16_t)(((((c & 0xF81F) * f + d * 0x0801) >> 6) & 0xF81F) |
                      ((((c & 0x07E0) * f + (d << 5)) >> 6) & 0x07E0));
}

// A pixel is under an eyelid unless its row lies in the column's open span.
#define LID_SHUT(packed, py) ((uint32_t)((py) - (int)((packed) & 0xFFFF)) > ((packed) >> 16))

// The row renderer. do_shade and do_swap are compile-time constants in each of
// the four copies below, so neither costs anything per pixel.
static inline __attribute__((always_inline)) void render_rows(
        const eye_renderer_t *r, const eye_frame_t *f, int y0, int nrows, uint16_t *out,
        const int stride, const bool do_shade, const bool do_swap) {
    const int size = r->size, half = r->half;
    const uint16_t *lut = r->lut;
    const uint16_t *iris = r->iris, *sclera = r->sclera;
    const int in = r->iris_n, sn = r->sclera_n;
    const int ic = in / 2, sc = sn / 2;
    const uint16_t lid = do_swap ? __builtin_bswap16(r->style->lid_color) : r->style->lid_color;
    const uint16_t back = do_swap ? __builtin_bswap16(r->style->back_color) : r->style->back_color;
    const uint32_t *lids = f->lid;
    const uint8_t *shade = r->shade;
    const uint16_t pupil = r->style->pupil_color;
    const int32_t slit_xc = r->slit_xc, slit_rc2 = r->slit_rc2;
    const int32_t spin_cos = f->spin_cos, spin_sin = f->spin_sin;
    const int32_t mz_u = f->m[2], mz_v = f->m[5], mz_w = f->m[8];
    // Stepping one pixel right moves x by 1 px = 256 in Q8.
    const int32_t du = f->m[0] * 256, dv = f->m[3] * 256, dw = f->m[6] * 256;

    for (int row = 0; row < nrows; row++) {
        const int py = y0 + row;
        uint16_t *line = out + row * stride;
        if (py < 0 || py >= size) {
            for (int x = 0; x < size; x++) line[x] = lid;
            continue;
        }
        const int y2 = 2 * py - size + 1;                 // 2 * (y offset from centre)
        const uint8_t *drow = dither4 + (py & 3) * 4;
        const int yq = py < half ? half - 1 - py : py - half;
        const uint16_t *zrow = r->ztab + yq * half;
        // The eyeball covers columns x_lo .. x_hi-1 of this row.
        const int zw = r->zwidth[yq];
        const int x_lo = half - zw, x_hi = half + zw;
        // Position terms in Q22 pixels: Q14 matrix * Q8 position.
        const int x2 = 2 * x_lo - size + 1;
        int32_t u_acc = f->m[0] * x2 * 128 + f->m[1] * y2 * 128;
        int32_t v_acc = f->m[3] * x2 * 128 + f->m[4] * y2 * 128;
        int32_t w_acc = f->m[6] * x2 * 128 + f->m[7] * y2 * 128;

        int px = 0;
        for (; px < x_lo; px++) line[px] = lid;

        // Left half walks the height table backwards, right half forwards.
        const uint16_t *zp = zrow + zw - 1;
        int zstep = -1;
        for (int part = 0; part < 2; part++) {
            const int end = part ? x_hi : half;
            for (; px < end; px++, zp += zstep, u_acc += du, v_acc += dv, w_acc += dw) {
                uint32_t c;
                if (LID_SHUT(lids[px], py)) {
                    c = lid;
                } else {
                    const int32_t z = *zp;
                    if (w_acc + mz_w * z <= 0) {
                        c = back;                          // far side of the eyeball
                    } else {
                        const int32_t u = (u_acc + mz_u * z) >> 16;   // Q6 px
                        const int32_t v = (v_acc + mz_v * z) >> 16;
                        const uint32_t e = lut[(uint32_t)(u * u + v * v) >> LUT_SHIFT];
                        const int32_t s = e & 0x7FFF;
                        const int tu = (u * s) >> 17, tv = (v * s) >> 17;
                        if (e & LUT_IRIS) {
                            c = iris[(ic + tv) * in + ic + tu];
                            if (slit_rc2) {                // inside the slit pupil?
                                // Undo the spin first: the slit stays upright.
                                const int32_t su = (u * spin_cos - v * spin_sin) >> 14;
                                const int32_t sv = (u * spin_sin + v * spin_cos) >> 14;
                                const int32_t a = ((su < 0 ? -su : su) >> 4) + slit_xc;
                                const int32_t b = sv >> 4;
                                if (a * a + b * b < slit_rc2) c = pupil;
                            }
                        } else {
                            c = sclera[(sc + tv) * sn + sc + tu];
                        }
                        if (do_shade) c = scale565(c, shade[z >> 8], drow[px & 3]);
                        if (do_swap) c = __builtin_bswap16((uint16_t)c);
                    }
                }
                line[px] = (uint16_t)c;
            }
            zp = zrow;
            zstep = 1;
        }

        for (; px < size; px++) line[px] = lid;

        if ((r->flags & EYE_FLAG_HIGHLIGHT) && py >= r->hl_y && py < r->hl_y + r->hl_n) {
            const uint8_t *a = r->hl + (py - r->hl_y) * r->hl_n;
            for (int i = 0; i < r->hl_n; i++) {
                const int hx = r->hl_x + i;
                if (!a[i] || LID_SHUT(lids[hx], py)) continue;
                // Blend toward white: c + (white - c) * a / 32.
                uint32_t c = line[hx];
                if (do_swap) c = __builtin_bswap16((uint16_t)c);
                const uint32_t inv = 32 - a[i];
                const uint32_t rb = ((c & 0xF81F) * inv + 0xF81F * a[i]) >> 5 & 0xF81F;
                const uint32_t g = ((c & 0x07E0) * inv + 0x07E0 * a[i]) >> 5 & 0x07E0;
                c = rb | g;
                line[hx] = do_swap ? __builtin_bswap16((uint16_t)c) : (uint16_t)c;
            }
        }
    }
}

void EYE_HOT(eye_frame_render)(const eye_renderer_t *r, const eye_frame_t *f,
                               int y0, int nrows, uint16_t *out, int stride) {
    const bool shade = r->flags & EYE_FLAG_SHADE, swap = r->flags & EYE_FLAG_SWAP;
    if (shade) {
        if (swap) render_rows(r, f, y0, nrows, out, stride, true, true);
        else render_rows(r, f, y0, nrows, out, stride, true, false);
    } else {
        if (swap) render_rows(r, f, y0, nrows, out, stride, false, true);
        else render_rows(r, f, y0, nrows, out, stride, false, false);
    }
}

void eye_render_rows(const eye_renderer_t *r, int y0, int nrows, uint16_t *out) {
    eye_frame_render(r, &r->frame, y0, nrows, out, r->size);
}
