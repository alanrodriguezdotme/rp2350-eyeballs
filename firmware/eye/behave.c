#include "behave.h"

#include <math.h>

#define BLINK_CLOSE_S 0.07f
#define BLINK_OPEN_S 0.15f
#define LID_FOLLOW_S 0.07f     // how quickly lids catch up with the gaze
#define PUPIL_FOLLOW_S 0.45f

// Random number in 0..1 (xorshift).
static float rnd(eye_behave_t *b) {
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static float between(eye_behave_t *b, float lo, float hi) { return lo + (hi - lo) * rnd(b); }

// Fraction of the way from the current value to its target after dt.
static float ease(float dt, float time_constant) { return 1.0f - expf(-dt / time_constant); }

void eye_behave_init(eye_behave_t *b, uint32_t seed) {
    *b = (eye_behave_t){0};
    b->rng = seed ? seed : 1;
    b->hold_left = 0.8f;
    b->blink_wait = between(b, 1.5f, 4.0f);
    b->pupil = b->pupil_target = 0.5f;
    b->pupil_wait = 2.0f;
    b->lid_upper = b->lid_lower = 0.7f;
}

static void start_blink(eye_behave_t *b) {
    if (b->blink_state == 0) {
        b->blink_state = 1;
        b->blink_t = 0;
    }
}

static void update_gaze(eye_behave_t *b, float dt) {
    if (b->moving) {
        b->move_t += dt;
        float t = b->move_t / b->move_dur;
        if (t >= 1.0f) {
            t = 1.0f;
            b->moving = false;
            // Mostly short looks, sometimes a long stare.
            float h = rnd(b);
            b->hold_left = 0.25f + h * h * 2.8f;
        }
        float s = t * t * (3.0f - 2.0f * t);             // ease in and out
        b->gaze_x = b->from_x + (b->to_x - b->from_x) * s;
        b->gaze_y = b->from_y + (b->to_y - b->from_y) * s;
        return;
    }
    b->hold_left -= dt;
    if (b->hold_left > 0) return;

    // Pick somewhere new: usually a small glance, sometimes right across.
    float reach = rnd(b) < 0.7f ? 0.45f : 0.95f;
    float radius = sqrtf(rnd(b)) * reach;
    float angle = rnd(b) * 6.2831853f;
    b->from_x = b->gaze_x;
    b->from_y = b->gaze_y;
    b->to_x = radius * cosf(angle);
    b->to_y = radius * sinf(angle) * 0.7f;               // eyes travel less up and down
    float dist = hypotf(b->to_x - b->from_x, b->to_y - b->from_y);
    b->move_dur = 0.07f + 0.10f * dist;                  // longer jumps take longer
    b->move_t = 0;
    b->moving = true;
    if (dist > 0.8f && rnd(b) < 0.5f) start_blink(b);    // big jumps often come with a blink
}

// Returns how shut the blink has the eye: 0 open .. 1 shut.
static float update_blink(eye_behave_t *b, float dt) {
    if (b->blink_state == 0) {
        b->blink_wait -= dt;
        if (b->blink_wait <= 0 || b->blinks_queued > 0) {
            if (b->blinks_queued > 0) b->blinks_queued--;
            else if (rnd(b) < 0.12f) b->blinks_queued = 1;   // now and then, a double blink
            b->blink_wait = between(b, 2.0f, 7.0f);
            start_blink(b);
        }
        return 0;
    }
    b->blink_t += dt;
    if (b->blink_state == 1) {
        if (b->blink_t < BLINK_CLOSE_S) return b->blink_t / BLINK_CLOSE_S;
        b->blink_state = 2;
        b->blink_t = 0;
        return 1;
    }
    if (b->blink_t < BLINK_OPEN_S) return 1.0f - b->blink_t / BLINK_OPEN_S;
    b->blink_state = 0;
    return 0;
}

static float clamp1(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

void eye_behave_update(eye_behave_t *b, const eye_renderer_t *r, const eye_reaction_t *react,
                       float dt, eye_pose_t *pose) {
    static const eye_reaction_t none = {0};
    if (!react) react = &none;
    if (dt > 0.1f) dt = 0.1f;

    update_gaze(b, dt);

    // Where the eye actually looks: its own glances, pulled toward the ground
    // by a tilt and left behind by a quick turn. Glances shrink while tilted.
    float tilt = hypotf(react->tilt_x, react->tilt_y);
    float gx = b->gaze_x * (1.0f - 0.5f * tilt) + react->tilt_x + react->lag_x;
    float gy = b->gaze_y * (1.0f - 0.5f * tilt) + react->tilt_y + react->lag_y;
    // Dizzy after a shake: the eye rolls in circles that die away.
    if (react->dizzy > 0) {
        b->dizzy_angle += dt * 6.2831853f * 1.8f;
        gx = gx * (1.0f - react->dizzy) + 0.8f * react->dizzy * cosf(b->dizzy_angle);
        gy = gy * (1.0f - react->dizzy) + 0.6f * react->dizzy * sinf(b->dizzy_angle);
    }
    gy += 0.3f * react->drowsy;                          // a sleepy eye sinks
    gx = clamp1(gx);
    gy = clamp1(gy);

    b->pupil_wait -= dt;
    if (b->pupil_wait <= 0) {
        b->pupil_target = between(b, 0.25f, 0.75f);
        b->pupil_wait = between(b, 2.0f, 5.0f);
    }
    if (react->shake > 0) {
        b->pupil += (0.0f - b->pupil) * ease(dt, 0.08f); // startled: pupil snaps small
    } else {
        b->pupil += (b->pupil_target - b->pupil) * ease(dt, PUPIL_FOLLOW_S);
    }

    float want_upper, want_lower;
    eye_track_lids(r, gx, gy, &want_upper, &want_lower);
    if (react->shake > 0) want_upper = want_lower = 1.0f;   // wide-eyed while shaken
    b->lid_upper += (want_upper - b->lid_upper) * ease(dt, LID_FOLLOW_S);
    b->lid_lower += (want_lower - b->lid_lower) * ease(dt, LID_FOLLOW_S);

    float open = 1.0f - update_blink(b, dt);
    if (react->shake > 0) open = 1.0f;                   // no blinking mid-shake
    open *= 1.0f - react->drowsy;                        // lids droop shut as it dozes off
    pose->gaze_x = gx;
    pose->gaze_y = gy;
    pose->pupil = b->pupil;
    pose->lid_upper = b->lid_upper * open;
    pose->lid_lower = b->lid_lower * open;

    // Some styles turn steadily (spirals, flames). Keep the angle small.
    b->spin += r->style->spin_rpm * (6.2831853f / 60.0f) * dt;
    if (b->spin > 6.2831853f) b->spin -= 6.2831853f;
    if (b->spin < -6.2831853f) b->spin += 6.2831853f;
    pose->spin = b->spin;
}
