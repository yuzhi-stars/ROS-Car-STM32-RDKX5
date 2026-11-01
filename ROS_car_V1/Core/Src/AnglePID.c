#include "AnglePID.h"

#define ANGLE_PID_ENABLE              1U
#define ANGLE_PID_ALLOW_MANUAL_YAW    0U
#define ANGLE_PID_MANUAL_DEADBAND_CPS 80L
#define ANGLE_PID_KP_CPS_PER_DEG      15.0f
#define ANGLE_PID_KI_CPS_PER_DEG      0.0f
#define ANGLE_PID_KD_CPS_PER_DEG      0.5f
#define ANGLE_PID_PERIOD_MS           20U
#define ANGLE_PID_OUTPUT_LIMIT_CPS    400L
#define ANGLE_PID_INTEGRAL_LIMIT      300.0f

static float angle_target_deg = 0.0f;
static float angle_current_deg = 0.0f;
static float angle_error_deg = 0.0f;
static float angle_last_error_deg = 0.0f;
static float angle_integral = 0.0f;
static uint32_t angle_last_update_tick = 0U;
static int32_t angle_last_output_cps = 0;

static float AnglePID_NormalizeDeg(float angle_deg)
{
  while (angle_deg > 180.0f)
  {
    angle_deg -= 360.0f;
  }

  while (angle_deg < -180.0f)
  {
    angle_deg += 360.0f;
  }

  return angle_deg;
}

static int32_t AnglePID_LimitOutput(float output)
{
  if (output > (float)ANGLE_PID_OUTPUT_LIMIT_CPS)
  {
    return ANGLE_PID_OUTPUT_LIMIT_CPS;
  }

  if (output < -(float)ANGLE_PID_OUTPUT_LIMIT_CPS)
  {
    return -ANGLE_PID_OUTPUT_LIMIT_CPS;
  }

  return (int32_t)output;
}

void AnglePID_Init(void)
{
  angle_target_deg = 0.0f;
  angle_current_deg = 0.0f;
  angle_error_deg = 0.0f;
  angle_last_error_deg = 0.0f;
  angle_integral = 0.0f;
  angle_last_update_tick = HAL_GetTick();
  angle_last_output_cps = 0;
}

void AnglePID_SetYawDeg(float yaw_deg)
{
  angle_current_deg = AnglePID_NormalizeDeg(yaw_deg);
}

void AnglePID_SetTargetDeg(float target_deg)
{
  angle_target_deg = AnglePID_NormalizeDeg(target_deg);
  angle_error_deg = 0.0f;
  angle_last_error_deg = 0.0f;
  angle_integral = 0.0f;
  angle_last_output_cps = 0;
}

void AnglePID_ResetTargetToCurrent(void)
{
  AnglePID_SetTargetDeg(angle_current_deg);
}

int32_t AnglePID_Update(int32_t manual_wz_cps)
{
#if ANGLE_PID_ENABLE
#if ANGLE_PID_ALLOW_MANUAL_YAW
  if ((manual_wz_cps > ANGLE_PID_MANUAL_DEADBAND_CPS) ||
      (manual_wz_cps < -ANGLE_PID_MANUAL_DEADBAND_CPS))
  {
    AnglePID_ResetTargetToCurrent();
    return manual_wz_cps;
  }
#else
  (void)manual_wz_cps;
#endif

  uint32_t now_ms = HAL_GetTick();
  uint32_t elapsed_ms = now_ms - angle_last_update_tick;

  if (elapsed_ms < ANGLE_PID_PERIOD_MS)
  {
    return angle_last_output_cps;
  }

  float dt_s = (float)elapsed_ms * 0.001f;
  angle_last_update_tick = now_ms;

  angle_error_deg = AnglePID_NormalizeDeg(angle_target_deg - angle_current_deg);
  angle_integral += angle_error_deg * dt_s;

  if (angle_integral > ANGLE_PID_INTEGRAL_LIMIT)
  {
    angle_integral = ANGLE_PID_INTEGRAL_LIMIT;
  }
  else if (angle_integral < -ANGLE_PID_INTEGRAL_LIMIT)
  {
    angle_integral = -ANGLE_PID_INTEGRAL_LIMIT;
  }

  float derivative = (angle_error_deg - angle_last_error_deg) / dt_s;
  float output = ANGLE_PID_KP_CPS_PER_DEG * angle_error_deg +
                 ANGLE_PID_KI_CPS_PER_DEG * angle_integral +
                 ANGLE_PID_KD_CPS_PER_DEG * derivative;

  angle_last_error_deg = angle_error_deg;
  angle_last_output_cps = AnglePID_LimitOutput(output);
  return angle_last_output_cps;
#else
  return manual_wz_cps;
#endif
}

int32_t AnglePID_UpdateWithTarget(float target_deg, float current_deg)
{
  angle_target_deg = AnglePID_NormalizeDeg(target_deg);
  angle_current_deg = AnglePID_NormalizeDeg(current_deg);
  return AnglePID_Update(0);
}

float AnglePID_GetTargetDeg(void)
{
  return angle_target_deg;
}

float AnglePID_GetErrorDeg(void)
{
  return angle_error_deg;
}
