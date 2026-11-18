// QMI8658 motion sensor: accelerometer and gyroscope, on the shared I2C bus.
#ifndef QMI8658_H
#define QMI8658_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} qmi8658_accel_raw_t;

// Accelerometer only, +/-2g. Used by the bring-up test.
bool qmi8658_init_accelerometer(void);
bool qmi8658_read_accel(qmi8658_accel_raw_t *raw);

// Accelerometer (+/-8g) and gyroscope (+/-512 degrees per second), both about
// 56 readings per second with the sensor's own low-pass filter on.
bool qmi8658_init_motion(void);

// Latest reading in the sensor's own axes: accel in g, gyro in degrees/second.
bool qmi8658_read_motion(float accel_g[3], float gyro_dps[3]);

#endif
