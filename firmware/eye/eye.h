// Textured eyeball renderer.
//
// The eye is a sphere seen straight on. Each screen pixel is projected onto
// the sphere, rotated by the gaze into the eye's own frame, and looked up in
// one of two flat textures: the iris (seen head-on) or the sclera (an
// azimuthal map out to 90 degrees from the pupil). Eyelids are a per-column
// top and bottom edge. There is no framebuffer: the caller asks for rows.
//
// Portable C. Nothing in here knows about the board.
#ifndef EYE_H
#define EYE_H

#include <stdbool.h>
#include <stdint.h>

#define EYE_MAX_SIZE 368   // largest eye, in pixels across
#define EYE_LUT_N 4096
#define EYE_TEXTURE_POOL 120000   // most pixels a style's iris + sclera may use

// One eye style. Built by tools/eyepack.py from a Monster Eyes style folder.
typedef struct {
    const char *name;
    uint16_t iris_n;          // iris texture is iris_n x iris_n
    uint16_t sclera_n;        // sclera texture is sclera_n x sclera_n
    const uint16_t *iris;     // RGB565; centre disc is the pupil colour
    const uint16_t *sclera;   // RGB565; radius maps to 0..90 degrees
    uint16_t lid_n;           // columns in the eyelid tables
    uint16_t lid_h;           // row units of the eyelid tables
    const uint8_t *upper_open, *upper_closed;   // upper lid edge per column
    const uint8_t *lower_open, *lower_closed;   // lower lid edge per column
    uint16_t back_color;      // back of the eyeball
    uint16_t lid_color;       // eyelids and everything outside the eyeball
    uint16_t pupil_color;     // used for slit pupils; round ones are in the iris texture
    float eye_radius;         // sphere radius, as a fraction of eye size
    float iris_radius;        // iris radius, as a fraction of eye size
    float iris_hole;          // where the iris starts in its texture (0..1)
    float pupil_min, pupil_max;   // pupil radius, as a fraction of the iris
    float slit_radius;        // slit pupil half-height, as a fraction of eye size; 0 = round
    float spin_rpm;           // the eyeball turns about its pupil; positive is clockwise
} eye_style_t;

// Every built-in style, in swipe order (firmware/eye/styles/index.c).
extern const eye_style_t *const eye_styles[];
extern const int eye_style_count;

// Everything that changes from frame to frame.
typedef struct {
    float gaze_x, gaze_y;     // -1..1, right and down positive
    float pupil;              // 0 smallest .. 1 largest
    float lid_upper;          // 0 closed .. 1 open
    float lid_lower;          // 0 closed .. 1 open
    float spin;               // radians the eyeball has turned about its pupil
} eye_pose_t;

#define EYE_FLAG_SHADE     1  // darken toward the edge of the eyeball
#define EYE_FLAG_HIGHLIGHT 2  // glossy highlight
#define EYE_FLAG_SWAP      4  // emit big-endian pixels (what the panel wants)

#define EYE_HL_MAX 64

// What changes per eye, per frame: where it looks and where its lids are.
// A renderer has one built in; a pair of eyes needs one each.
typedef struct {
    int32_t m[9];                        // gaze rotation, Q14
    int32_t spin_cos, spin_sin;          // Q14, to keep a slit pupil upright on a spinning eye
    uint32_t lid[EYE_MAX_SIZE];          // per column: top row | (open rows - 1) << 16
} eye_frame_t;

typedef struct {
    int size, half;
    unsigned flags;
    const eye_style_t *style;
    const uint16_t *iris, *sclera;      // textures actually sampled
    int iris_n, sclera_n;               // and their sizes
    float radius_px, iris_px;
    float last_pupil;
    int32_t slit_xc, slit_rc2;           // slit pupil: two arcs, in quarter pixels; 0 = round
    eye_frame_t frame;                   // used by eye_begin_frame / eye_render_rows
    uint16_t zwidth[EYE_MAX_SIZE / 2];   // per quadrant row: columns inside the eyeball
    uint16_t lut[EYE_LUT_N];             // radius^2 -> texture scale
    uint8_t shade[256];
    int hl_x, hl_y, hl_n;                // highlight box
    uint8_t hl[EYE_HL_MAX * EYE_HL_MAX];
    uint16_t ztab[(EYE_MAX_SIZE / 2) * (EYE_MAX_SIZE / 2)];  // sphere height, Q8
} eye_renderer_t;

// size must be even and at most EYE_MAX_SIZE. iris and sclera are the
// textures to sample, usually RAM copies (faster than reading flash), with
// their sizes; they may be smaller than the style's own, for a small eye.
// Pass NULL and 0 to sample the style's own arrays.
void eye_init(eye_renderer_t *r, int size, const eye_style_t *style,
              const uint16_t *iris, int iris_n, const uint16_t *sclera, int sclera_n,
              unsigned flags);

// One eye: call eye_begin_frame once per frame, then ask for rows.
void eye_begin_frame(eye_renderer_t *r, const eye_pose_t *pose);

// Render rows y0 .. y0+nrows-1 into out (nrows * size pixels). Only reads the
// renderer, so two cores may render different rows of one frame at once.
void eye_render_rows(const eye_renderer_t *r, int y0, int nrows, uint16_t *out);

// Several eyes from one renderer (same style, size and pupil): set up a frame
// for each, then render each into its own columns. mirror_lids flips the
// eyelid shape left to right, for the other eye of a pair. stride is the
// distance in pixels from one output row to the next.
void eye_frame_setup(eye_renderer_t *r, const eye_pose_t *pose, bool mirror_lids,
                     eye_frame_t *frame);
void eye_frame_render(const eye_renderer_t *r, const eye_frame_t *frame,
                      int y0, int nrows, uint16_t *out, int stride);

// Where relaxed eyelids sit for a given gaze: the upper lid rests just over
// the top of the iris and the lower lid just under it, so both follow the eye
// as it looks up and down. Results are lid openness values, 0..1.
void eye_track_lids(const eye_renderer_t *r, float gaze_x, float gaze_y,
                    float *upper, float *lower);

#endif
