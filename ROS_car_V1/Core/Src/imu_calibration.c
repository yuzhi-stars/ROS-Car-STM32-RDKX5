#include "imu_calibration.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

void IMU_Calibration_Init(IMU_Calibration_t *s)
{
    if (s != NULL) memset(s, 0, sizeof(*s));
}

static void Reject(IMU_Calibration_t *s, IMU_CalibrationReason_t reason, uint32_t now)
{
    if (s->samples != 0U) ++s->rejected_windows;
    s->samples = 0U;
    memset(s->mean, 0, sizeof(s->mean));
    memset(s->m2, 0, sizeof(s->m2));
    s->last_reason = reason;
    s->retry_ms = now;
    s->retry_wait = 1U;
}

uint8_t IMU_Calibration_Update(IMU_Calibration_t *s, const float gyro[3],
    const float accel[3], uint8_t fresh, uint32_t now, uint8_t may_calibrate)
{
    if (s == NULL) return 0U;
    if (s->valid != 0U) return 1U; /* Never recalibrate a valid bias in motion. */
    if ((fresh & 1U) != 0U && accel != NULL)
    {
        float norm2 = accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2];
        s->gravity_ok = (uint8_t)(isfinite(norm2) && norm2 >= 0.95f*0.95f && norm2 <= 1.05f*1.05f);
        s->last_accel_ms = now;
        s->have_accel = 1U;
    }
    if (may_calibrate == 0U) { Reject(s, IMU_CAL_DRIVE, now); return 0U; }
    if ((s->have_accel == 0U) || (uint32_t)(now - s->last_accel_ms) > IMU_CAL_ACCEL_MAX_AGE_MS)
    { Reject(s, IMU_CAL_ACCEL, now); return 0U; }
    if (s->gravity_ok == 0U) { Reject(s, IMU_CAL_GRAVITY, now); return 0U; }
    if ((fresh & 2U) == 0U || gyro == NULL)
    {
        if (s->have_gyro != 0U && (uint32_t)(now - s->last_gyro_ms) > IMU_CAL_GYRO_MAX_GAP_MS)
            Reject(s, IMU_CAL_GAP, now);
        return 0U;
    }
    uint32_t elapsed = (uint32_t)(now - s->last_gyro_ms);
    uint8_t had_gyro = s->have_gyro;
    s->last_gyro_ms = now;
    s->have_gyro = 1U;
    for (uint8_t i = 0U; i < 3U; ++i)
        if (!isfinite(gyro[i]) || fabsf(gyro[i]) > 600.0f)
        { Reject(s, IMU_CAL_GYRO, now); return 0U; }
    if (had_gyro != 0U && elapsed > IMU_CAL_GYRO_MAX_GAP_MS && s->samples != 0U)
    { Reject(s, IMU_CAL_GAP, now); return 0U; }
    if (s->retry_wait != 0U)
    {
        if ((uint32_t)(now - s->retry_ms) < IMU_CAL_RETRY_MS) return 0U;
        s->retry_wait = 0U;
    }
    if (s->samples == 0U) s->started_ms = now;
    ++s->samples;
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        /* Welford variance in float32, bounded raw range, no soft double. */
        float delta = gyro[i] - s->mean[i];
        s->mean[i] += delta / (float)s->samples;
        s->m2[i] += delta * (gyro[i] - s->mean[i]);
    }
    if (s->samples < IMU_CAL_MIN_SAMPLES || (uint32_t)(now - s->started_ms) < IMU_CAL_STILL_MS)
        return 0U;
    for (uint8_t i = 0U; i < 3U; ++i)
        if (!isfinite(s->m2[i]) || s->m2[i] / (float)(s->samples - 1U) > 2500.0f)
        { Reject(s, IMU_CAL_NOISE, now); return 0U; }
    memcpy(s->bias_raw, s->mean, sizeof(s->bias_raw));
    s->valid = 1U;
    s->last_reason = IMU_CAL_NONE;
    return 1U;
}
