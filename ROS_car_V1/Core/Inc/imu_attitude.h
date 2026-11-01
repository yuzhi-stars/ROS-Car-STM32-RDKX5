#ifndef IMU_ATTITUDE_H
#define IMU_ATTITUDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mahony six-axis complementary filter, independent of STM32/HAL.
 * q=(x,y,z,w) rotates SENSOR vectors into a right-handed, z-UP world.
 * Positive rotation follows the right-hand rule. Initial heading is zero;
 * gravity constrains roll/pitch, NOT absolute heading or yaw drift.
 * Inputs: bias-corrected gyro rad/s, specific force m/s^2 (retain gravity).
 * Call only for a NEW gyro sample, with actual elapsed seconds.
 * References/design details: docs/imu_attitude.md. */
#define IMU_ATTITUDE_GRAVITY 9.80665f
#define IMU_ATTITUDE_KP 2.0f
#define IMU_ATTITUDE_KI 0.05f
#define IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S 0.05f
#define IMU_ATTITUDE_MAX_DT_S 0.050f
#define IMU_ATTITUDE_INIT_SAMPLES 32U
#define IMU_ATTITUDE_ALIGN_ACCEL_GAP_S 0.030f

typedef struct
{
    float xyzw[4];
    float integral[3];
    float alignment_sum[3];
    float alignment_accel_gap_s;
    uint16_t alignment_samples;
    uint8_t initialized;
} IMU_Attitude_t;

/* Reset invalidates orientation; a new stationary alignment is required. */
void IMU_Attitude_Init(IMU_Attitude_t *state);

/* accel may be NULL when not freshly sampled. Returns 1 only if this NEW
 * gyro sample produced a valid quaternion. Startup needs 32 fresh near-static
 * accel samples; missing accel <=30 ms pauses counting, never reuses old data.
 * Motion, invalid accel or a longer outage restarts the stationary window.
 * Missing accel after startup allows gyro prediction.
 * Invalid gyro or dt resets alignment; never bridges a >50 ms sampling gap. */
uint8_t IMU_Attitude_Update(IMU_Attitude_t *state, const float gyro[3],
                             const float accel[3], float dt_s);

#ifdef __cplusplus
}
#endif
#endif
