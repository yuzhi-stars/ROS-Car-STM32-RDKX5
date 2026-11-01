#include "imu_attitude.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static float Attitude_Norm3(const float v[3])
{
    if (v == NULL) return 0.0f;
    for (uint8_t i = 0U; i < 3U; ++i)
        if (!isfinite(v[i])) return 0.0f;
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static uint8_t Attitude_Normalize(float q[4])
{
    float sum = 0.0f;
    for (uint8_t i = 0U; i < 4U; ++i) sum += q[i] * q[i];
    if (!isfinite(sum) || sum < 1.0e-12f) return 0U;
    float inv_norm = 1.0f / sqrtf(sum);
    for (uint8_t i = 0U; i < 4U; ++i) q[i] *= inv_norm;
    return 1U;
}

void IMU_Attitude_Init(IMU_Attitude_t *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->xyzw[3] = 1.0f;  /* Storage default, not a valid measurement. */
}

static uint8_t Attitude_Align(IMU_Attitude_t *state, const float gyro[3],
                               const float accel[3], float accel_norm, float dt_s)
{
    /* Alignment assumes rest, including no constant rotation. Gyro must have
     * undergone stationary bias calibration; it cannot be inferred from gravity. */
    float gyro_norm = Attitude_Norm3(gyro);
    state->alignment_accel_gap_s += dt_s;
    if (accel == NULL && gyro_norm <= 0.10f)
    {
        if (state->alignment_accel_gap_s <= IMU_ATTITUDE_ALIGN_ACCEL_GAP_S + 1.0e-6f)
            return 0U; /* Pause only: no duplicate samples and no fake attitude. */
    }
    if ((accel == NULL) || !isfinite(accel_norm) ||
        (accel_norm < 0.95f * IMU_ATTITUDE_GRAVITY) ||
        (accel_norm > 1.05f * IMU_ATTITUDE_GRAVITY) || (gyro_norm > 0.10f))
    {
        state->alignment_samples = 0U;
        memset(state->alignment_sum, 0, sizeof(state->alignment_sum));
        state->alignment_accel_gap_s = 0.0f;
        return 0U;
    }
    if (state->alignment_accel_gap_s > IMU_ATTITUDE_ALIGN_ACCEL_GAP_S + 1.0e-6f)
    {
        state->alignment_samples = 0U;
        memset(state->alignment_sum, 0, sizeof(state->alignment_sum));
    }
    state->alignment_accel_gap_s = 0.0f;
    for (uint8_t i = 0U; i < 3U; ++i)
        state->alignment_sum[i] += accel[i] / accel_norm;
    if (++state->alignment_samples < IMU_ATTITUDE_INIT_SAMPLES) return 0U;

    /* Reject an average spanning inconsistent gravity directions. */
    float norm = Attitude_Norm3(state->alignment_sum);
    if (!isfinite(norm) || norm < 0.995f * (float)state->alignment_samples)
    {
        IMU_Attitude_Init(state);
        return 0U;
    }
    float ax = state->alignment_sum[0] / norm;
    float ay = state->alignment_sum[1] / norm;
    float az = state->alignment_sum[2] / norm;
    float roll = atan2f(ay, az);
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    float sr = sinf(0.5f * roll), cr = cosf(0.5f * roll);
    float sp = sinf(0.5f * pitch), cp = cosf(0.5f * pitch);
    /* R = Rz(0) Ry(pitch) Rx(roll), so initial relative heading is zero. */
    state->xyzw[0] = sr * cp;
    state->xyzw[1] = cr * sp;
    state->xyzw[2] = -sr * sp;
    state->xyzw[3] = cr * cp;
    state->initialized = Attitude_Normalize(state->xyzw);
    return state->initialized;
}

uint8_t IMU_Attitude_Update(IMU_Attitude_t *state, const float gyro[3],
                             const float accel[3], float dt_s)
{
    if (state == NULL) return 0U;
    if ((gyro == NULL) || !isfinite(dt_s) || (dt_s <= 0.0f) ||
        (dt_s > IMU_ATTITUDE_MAX_DT_S))
    {
        IMU_Attitude_Init(state);
        return 0U;
    }
    for (uint8_t i = 0U; i < 3U; ++i)
        if (!isfinite(gyro[i]) || fabsf(gyro[i]) > 10.0f)
        {
            /* Beyond this project's +/-250 dps sensor range, with margin. */
            IMU_Attitude_Init(state);
            return 0U;
        }
    float accel_norm = Attitude_Norm3(accel);
    if (state->initialized == 0U) return Attitude_Align(state, gyro, accel, accel_norm, dt_s);

    float x = state->xyzw[0], y = state->xyzw[1];
    float z = state->xyzw[2], w = state->xyzw[3];
    float corrected[3] = {gyro[0], gyro[1], gyro[2]};
    /* Confidence tapers from 1 at 1g to 0 at +/-0.15g. Acceleration during
     * impact/free-fall must not be mistaken for the gravity direction. */
    float weight = 0.0f;
    if (isfinite(accel_norm) && accel_norm > 0.0f)
        weight = 1.0f - fabsf(accel_norm / IMU_ATTITUDE_GRAVITY - 1.0f) / 0.15f;
    if ((accel != NULL) && weight > 0.0f)
    {
        float inverse_accel = 1.0f / accel_norm;
        float a[3] = {accel[0] * inverse_accel, accel[1] * inverse_accel, accel[2] * inverse_accel};
        /* Predicted upward specific force in sensor axes: R(q)^T [0,0,1]. */
        float v[3] = {2.0f * (x * z - w * y), 2.0f * (y * z + w * x),
                      1.0f - 2.0f * (x * x + y * y)};
        float error[3] = {a[1] * v[2] - a[2] * v[1],
                          a[2] * v[0] - a[0] * v[2],
                          a[0] * v[1] - a[1] * v[0]};
        float alignment = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
        /* Reject >30 degree innovations rather than abruptly tilting on an
         * inconsistent acceleration. Startup tilt is handled by alignment. */
        if (alignment >= 0.8660254f)
        {
            float gyro_squared = gyro[0]*gyro[0] + gyro[1]*gyro[1] + gyro[2]*gyro[2];
            uint8_t adapt_bias = (uint8_t)((weight > 0.8f) && (gyro_squared < 0.01f));
            for (uint8_t i = 0U; i < 3U; ++i)
            {
                /* Integrate only near 1g and low angular rate to limit windup.
                 * Six-axis feedback cannot observe bias about world vertical. */
                if (adapt_bias != 0U)
                {
                    state->integral[i] += IMU_ATTITUDE_KI * weight * error[i] * dt_s;
                    if (state->integral[i] > IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S)
                        state->integral[i] = IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S;
                    if (state->integral[i] < -IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S)
                        state->integral[i] = -IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S;
                }
                corrected[i] += IMU_ATTITUDE_KP * weight * error[i];
            }
        }
    }
    for (uint8_t i = 0U; i < 3U; ++i) corrected[i] += state->integral[i];

    /* Quaternion exponential update q <- q * exp(omega*dt/2), instead of
     * unnormalized Euler integration. Sample time is never assumed constant. */
    float rate_squared = corrected[0]*corrected[0] + corrected[1]*corrected[1] + corrected[2]*corrected[2];
    if (!isfinite(rate_squared))
    {
        IMU_Attitude_Init(state);
        return 0U;
    }
    float half_angle_squared = 0.25f * rate_squared * dt_s * dt_s;
    float scale, dw;
    if (half_angle_squared <= 0.0025f)
    {
        /* |half-angle| <= 0.05 rad: fourth-order sinc/cos error is below
         * float32 precision. The usual 100 Hz path needs no trig/rate sqrt. */
        float fourth = half_angle_squared * half_angle_squared;
        scale = 0.5f * dt_s * (1.0f - half_angle_squared / 6.0f + fourth / 120.0f);
        dw = 1.0f - 0.5f * half_angle_squared + fourth / 24.0f;
    }
    else
    {
        float rate = sqrtf(rate_squared);
        float half_angle = 0.5f * rate * dt_s;
        scale = sinf(half_angle) / rate;
        dw = cosf(half_angle);
    }
    float dx = corrected[0] * scale, dy = corrected[1] * scale;
    float dz = corrected[2] * scale;
    state->xyzw[0] = w * dx + x * dw + y * dz - z * dy;
    state->xyzw[1] = w * dy - x * dz + y * dw + z * dx;
    state->xyzw[2] = w * dz + x * dy - y * dx + z * dw;
    state->xyzw[3] = w * dw - x * dx - y * dy - z * dz;
    if (Attitude_Normalize(state->xyzw) == 0U)
    {
        IMU_Attitude_Init(state);
        return 0U;
    }
    return 1U;
}
