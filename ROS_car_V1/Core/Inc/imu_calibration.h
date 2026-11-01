#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define IMU_CAL_MIN_SAMPLES 200U
#define IMU_CAL_STILL_MS 2000U
#define IMU_CAL_ACCEL_MAX_AGE_MS 30U
#define IMU_CAL_GYRO_MAX_GAP_MS 50U
#define IMU_CAL_RETRY_MS 250U

typedef enum {
    IMU_CAL_NONE, IMU_CAL_SENSOR, IMU_CAL_DRIVE, IMU_CAL_ACCEL,
    IMU_CAL_GRAVITY, IMU_CAL_GYRO, IMU_CAL_GAP, IMU_CAL_NOISE
} IMU_CalibrationReason_t;

typedef struct {
    float mean[3], m2[3], bias_raw[3];
    uint32_t started_ms, last_accel_ms, last_gyro_ms, retry_ms;
    uint32_t rejected_windows;
    uint16_t samples;
    uint8_t valid, have_accel, have_gyro, gravity_ok, retry_wait;
    IMU_CalibrationReason_t last_reason;
} IMU_Calibration_t;

void IMU_Calibration_Init(IMU_Calibration_t *state);
/* Main only. raw gyro in sensor LSB, accel in g; fresh bit0=accel, bit1=gyro.
 * Each new gyro contributes once; a recent independently sampled accel may
 * qualify it. may_calibrate must be zero while wheel targets/motion exist.
 * Returns 1 once a >=2 s, >=200-sample stationary window succeeds. */
uint8_t IMU_Calibration_Update(IMU_Calibration_t *state, const float gyro_raw[3],
    const float accel_g[3], uint8_t fresh, uint32_t now_ms, uint8_t may_calibrate);
#ifdef __cplusplus
}
#endif
#endif
