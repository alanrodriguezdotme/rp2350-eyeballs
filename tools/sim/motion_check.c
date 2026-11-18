// Feeds made-up motion into the motion detector and prints what it reports,
// to check the thresholds without a board.
//   cc -O2 -I../../firmware/eye -o out/motion_check motion_check.c ../../firmware/eye/motion.c -lm
#include <math.h>
#include <stdio.h>

#include "motion.h"

#define DT (1.0f / 30.0f)
#define PI 3.14159265f

typedef void (*scenario_fn)(float t, eye_imu_t *imu);

// The pendant hangs upright: the ground is toward the bottom of the screen,
// so the accelerometer reads 1g toward the top (negative y).
static void upright(eye_imu_t *imu) { *imu = (eye_imu_t){0, -1, 0, 0.8f, -0.5f, 0.3f}; }

static void still(float t, eye_imu_t *imu) { (void)t; upright(imu); }

static void walking(float t, eye_imu_t *imu) {     // bounce at 2Hz, swing at 1.5Hz
    upright(imu);
    imu->ay -= 0.35f * sinf(2 * PI * 2.0f * t);
    imu->ax += 0.05f * sinf(2 * PI * 1.5f * t);
    imu->gz += 70.0f * sinf(2 * PI * 1.5f * t);
    imu->gy += 30.0f * sinf(2 * PI * 1.0f * t);
}

static void tilt(float t, eye_imu_t *imu) {        // rolled 30 degrees from t=1s
    upright(imu);
    if (t >= 1.0f) { imu->ax = -sinf(PI / 6); imu->ay = -cosf(PI / 6); }
}

static void quick_turn(float t, eye_imu_t *imu) {  // 300 dps about vertical, 1.0s to 1.2s
    upright(imu);
    if (t >= 1.0f && t < 1.2f) imu->gy += 300.0f;
}

static void shake(float t, eye_imu_t *imu) {       // +/-2.5g at 5Hz, 1s to 2.5s
    upright(imu);
    if (t >= 1.0f && t < 2.5f) imu->ax += 2.5f * sinf(2 * PI * 5.0f * t);
}

static void run(const char *name, scenario_fn fn, float seconds, float print_every) {
    eye_motion_t m;
    eye_reaction_t r = {0};
    eye_motion_init(&m);
    float peak_tilt = 0, peak_lag = 0, peak_dizzy = 0, peak_drowsy = 0, sleep_at = -1;
    int shake_frames = 0;
    printf("\n%s\n", name);
    for (int i = 0; i * DT < seconds; i++) {
        float t = i * DT;
        eye_imu_t imu;
        fn(t, &imu);
        eye_motion_update(&m, &imu, DT, &r);
        float tl = hypotf(r.tilt_x, r.tilt_y), lg = hypotf(r.lag_x, r.lag_y);
        if (tl > peak_tilt) peak_tilt = tl;
        if (lg > peak_lag) peak_lag = lg;
        if (r.dizzy > peak_dizzy) peak_dizzy = r.dizzy;
        if (r.drowsy > peak_drowsy) peak_drowsy = r.drowsy;
        if (r.shake > 0) shake_frames++;
        if (r.sleep && sleep_at < 0) sleep_at = t;
        if (print_every > 0 && fmodf(t + DT / 2, print_every) < DT)
            printf("  t=%5.2f tilt %+.2f %+.2f  lag %+.2f %+.2f  shake %.0f dizzy %.2f drowsy %.2f\n",
                   t, r.tilt_x, r.tilt_y, r.lag_x, r.lag_y, r.shake, r.dizzy, r.drowsy);
    }
    printf("  peaks: tilt %.2f, lag %.2f, dizzy %.2f, drowsy %.2f; shaking for %.2fs; sleep at %.1fs\n",
           peak_tilt, peak_lag, peak_dizzy, peak_drowsy, shake_frames * DT, sleep_at);
}

int main(void) {
    run("still for 70s (expect: drowsy from 40s, sleep at 60s, nothing else)", still, 70, 0);
    run("walking for 70s (expect: no reactions, no sleep)", walking, 70, 0);
    run("30 degree tilt at 1s, held (expect: gaze toward the low side, fading over seconds)", tilt, 12, 1.0f);
    run("quick turn at 1s (expect: a brief sideways lag)", quick_turn, 2.5f, 0.1f);
    run("shake from 1s to 2.5s (expect: shake, then dizzy fading)", shake, 6, 0.5f);
    return 0;
}
