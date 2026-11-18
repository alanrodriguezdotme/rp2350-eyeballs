// Turns motion-sensor readings into things for the eye to react to.
//
// Only deliberate movement counts. A pendant swinging and bouncing on a walk
// should produce nothing; picking it up and tilting, turning or shaking it
// should. Portable C: it takes plain numbers, so it can be tested on a
// computer with made-up motion.
#ifndef EYE_MOTION_H
#define EYE_MOTION_H

#include <stdbool.h>

// One sensor reading, in the screen's frame: x right, y down, z out of the
// screen toward the viewer. Accel in g, rotation rate in degrees per second.
// At rest the accelerometer reads 1g pointing up, away from the ground.
typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
} eye_imu_t;

typedef struct {
    float tilt_x, tilt_y;    // gaze offset toward the ground after a tilt
    float lag_x, lag_y;      // gaze offset left behind by a quick turn
    float shake;             // 1 while being shaken
    float dizzy;             // 1 fading to 0 after a shake stops
    float drowsy;            // 0 awake .. 1 asleep, from lying still
    bool sleep;              // still for long enough: time to turn off
} eye_reaction_t;

typedef struct {
    bool started;
    float lp[3];             // smoothed accel
    float slow[3];           // very smoothed accel, for the shake detector
    float base_x, base_y;    // where "down" usually points on screen
    float gyro_bias[3];
    float tilt_x, tilt_y, lag_x, lag_y;
    float shake_env, shake_time;
    bool shaking;
    float dizzy;
    float still_time;
} eye_motion_t;

// Seconds of stillness before the eye starts to droop, and before it sleeps.
#define EYE_DROWSY_AFTER_S 40.0f
#define EYE_SLEEP_AFTER_S 60.0f

void eye_motion_init(eye_motion_t *m);
void eye_motion_update(eye_motion_t *m, const eye_imu_t *imu, float dt, eye_reaction_t *out);

// True if this reading alone shows clear movement. Used to wake from sleep.
bool eye_motion_is_moving(const eye_motion_t *m, const eye_imu_t *imu);

#endif
