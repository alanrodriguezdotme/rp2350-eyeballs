#include "motion.h"

#include <math.h>

// Tilt
#define TILT_SMOOTH_S 0.12f       // smoothing on the accelerometer
#define TILT_BASELINE_S 6.0f      // how fast a held tilt becomes the new normal
#define TILT_DEADBAND 0.10f       // about 6 degrees: smaller tilts do nothing
#define TILT_GAIN 2.2f            // a 30 degree tilt gives nearly full gaze
#define TILT_OUT_S 0.15f
// Quick turns
#define LAG_DEADBAND_DPS 100.0f   // slower turns than this do nothing
#define LAG_FULL_DEG 40.0f        // this much fast turn gives full gaze offset
#define LAG_RETURN_S 0.35f
// Shake
#define SHAKE_SLOW_S 0.5f
#define SHAKE_LEVEL_G 0.8f        // walking bounce stays well under this
#define SHAKE_HOLD_S 0.3f         // must last this long to count
#define DIZZY_S 2.5f
// Stillness
#define STILL_GYRO_DPS 3.0f
#define STILL_ACCEL_G 0.04f
#define GYRO_BIAS_S 3.0f

static float ease(float dt, float time_constant) { return 1.0f - expf(-dt / time_constant); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void eye_motion_init(eye_motion_t *m) { *m = (eye_motion_t){0}; }

// Remove a dead zone around zero, keeping the sign.
static float deadband(float v, float zone) {
    if (v > zone) return v - zone;
    if (v < -zone) return v + zone;
    return 0;
}

void eye_motion_update(eye_motion_t *m, const eye_imu_t *imu, float dt, eye_reaction_t *out) {
    const float a[3] = {imu->ax, imu->ay, imu->az};
    const float g[3] = {imu->gx, imu->gy, imu->gz};
    if (dt > 0.1f) dt = 0.1f;

    if (!m->started) {
        for (int i = 0; i < 3; i++) {
            m->lp[i] = m->slow[i] = a[i];
            m->gyro_bias[i] = g[i];
        }
    }

    float hp2 = 0, rate2 = 0, rate[3];
    for (int i = 0; i < 3; i++) {
        m->lp[i] += (a[i] - m->lp[i]) * ease(dt, TILT_SMOOTH_S);
        m->slow[i] += (a[i] - m->slow[i]) * ease(dt, SHAKE_SLOW_S);
        float hp = a[i] - m->slow[i];             // the fast-changing part
        hp2 += hp * hp;
        // The gyro reads a small constant offset at rest; track and remove it.
        m->gyro_bias[i] += (g[i] - m->gyro_bias[i]) * ease(dt, GYRO_BIAS_S);
        rate[i] = g[i] - m->gyro_bias[i];
        rate2 += rate[i] * rate[i];
    }
    const float hp_mag = sqrtf(hp2), rate_mag = sqrtf(rate2);

    // --- Tilt: which way is the ground, compared with where it usually is?
    const float lp_mag = sqrtf(m->lp[0] * m->lp[0] + m->lp[1] * m->lp[1] + m->lp[2] * m->lp[2]);
    float want_x = 0, want_y = 0;
    if (lp_mag > 0.6f && lp_mag < 1.4f) {         // near 1g: not being bounced
        const float down_x = -m->lp[0] / lp_mag, down_y = -m->lp[1] / lp_mag;
        if (!m->started) {
            m->base_x = down_x;
            m->base_y = down_y;
        }
        m->base_x += (down_x - m->base_x) * ease(dt, TILT_BASELINE_S);
        m->base_y += (down_y - m->base_y) * ease(dt, TILT_BASELINE_S);
        const float dx = down_x - m->base_x, dy = down_y - m->base_y;
        const float mag = sqrtf(dx * dx + dy * dy);
        if (mag > TILT_DEADBAND) {
            const float k = clampf((mag - TILT_DEADBAND) * TILT_GAIN, 0, 1) / mag;
            want_x = dx * k;
            want_y = dy * k;
        }
    }
    m->tilt_x += (want_x - m->tilt_x) * ease(dt, TILT_OUT_S);
    m->tilt_y += (want_y - m->tilt_y) * ease(dt, TILT_OUT_S);

    // --- Quick turns: the eye stays behind, then catches up.
    // Turning about the screen's vertical axis moves the gaze sideways;
    // about its horizontal axis, up and down.
    m->lag_x -= deadband(rate[1], LAG_DEADBAND_DPS) * dt / LAG_FULL_DEG;
    m->lag_y += deadband(rate[0], LAG_DEADBAND_DPS) * dt / LAG_FULL_DEG;
    m->lag_x = clampf(m->lag_x * (1.0f - ease(dt, LAG_RETURN_S)), -1, 1);
    m->lag_y = clampf(m->lag_y * (1.0f - ease(dt, LAG_RETURN_S)), -1, 1);

    // --- Shake: hard, sustained back-and-forth.
    m->shake_env += (hp_mag - m->shake_env) * ease(dt, hp_mag > m->shake_env ? 0.05f : 0.25f);
    if (m->shake_env > SHAKE_LEVEL_G) {
        m->shake_time += dt;
    } else {
        m->shake_time = 0;
    }
    const bool shaking = m->shake_time > SHAKE_HOLD_S;
    if (m->shaking && !shaking) m->dizzy = 1.0f;
    m->shaking = shaking;
    if (!shaking && m->dizzy > 0) m->dizzy = clampf(m->dizzy - dt / DIZZY_S, 0, 1);

    // --- Stillness.
    if (rate_mag < STILL_GYRO_DPS && hp_mag < STILL_ACCEL_G) {
        m->still_time += dt;
    } else if (rate_mag > 3 * STILL_GYRO_DPS || hp_mag > 3 * STILL_ACCEL_G) {
        m->still_time = 0;
    }

    m->started = true;
    out->tilt_x = m->tilt_x;
    out->tilt_y = m->tilt_y;
    out->lag_x = m->lag_x;
    out->lag_y = m->lag_y;
    out->shake = shaking ? 1.0f : 0.0f;
    out->dizzy = m->dizzy;
    out->drowsy = clampf((m->still_time - EYE_DROWSY_AFTER_S) /
                         (EYE_SLEEP_AFTER_S - EYE_DROWSY_AFTER_S), 0, 1);
    out->sleep = m->still_time >= EYE_SLEEP_AFTER_S;
}

bool eye_motion_is_moving(const eye_motion_t *m, const eye_imu_t *imu) {
    const float a[3] = {imu->ax, imu->ay, imu->az};
    const float g[3] = {imu->gx, imu->gy, imu->gz};
    float hp2 = 0, rate2 = 0;
    for (int i = 0; i < 3; i++) {
        hp2 += (a[i] - m->slow[i]) * (a[i] - m->slow[i]);
        rate2 += (g[i] - m->gyro_bias[i]) * (g[i] - m->gyro_bias[i]);
    }
    return sqrtf(rate2) > 20.0f || sqrtf(hp2) > 0.15f;
}
