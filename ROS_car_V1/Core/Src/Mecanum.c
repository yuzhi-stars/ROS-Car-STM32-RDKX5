#include "Mecanum.h"
#include "Motor.h"

#define MECANUM_DEFAULT_LIMIT_CPS 10000L

static int32_t Mecanum_Abs32(int32_t value);

void Mecanum_SetVelocity(int32_t vx_cps, int32_t vy_cps, int32_t wz_cps)
{
  Mecanum_SetVelocityLimited(vx_cps, vy_cps, wz_cps, MECANUM_DEFAULT_LIMIT_CPS);
}

void Mecanum_SetVelocityLimited(int32_t vx_cps,
                                int32_t vy_cps,
                                int32_t wz_cps,
                                int32_t limit_cps)
{
  int32_t targets[4];
  Mecanum_CalcMotorTargets(vx_cps, vy_cps, wz_cps, limit_cps, targets);

  /*
   * User motor number mapping:
   *   motor1 = right-front
   *   motor2 = left-front
   *   motor3 = right-rear
   *   motor4 = left-rear
   *
   * Existing Motor_ID_t mapping:
   *   MOTOR_LEFT_FRONT
   *   MOTOR_RIGHT_FRONT
   *   MOTOR_LEFT_REAR
   *   MOTOR_RIGHT_REAR
   */
  for (uint8_t i = 0U; i < MOTOR_COUNT; ++i)
    Motor_ClosedLoopSetTarget((Motor_ID_t)i, targets[i]);
}

void Mecanum_CalcMotorTargets(int32_t vx_cps, int32_t vy_cps, int32_t wz_cps,
    int32_t limit_cps, int32_t targets[4])
{
  int32_t wheel[4];
  if (targets == NULL) return;
  Mecanum_CalcWheelTargets(vx_cps, vy_cps, wz_cps, wheel);
  Mecanum_NormalizeTargets(wheel, limit_cps);
  targets[MOTOR_LEFT_FRONT] = wheel[1];
  targets[MOTOR_RIGHT_FRONT] = wheel[0];
  targets[MOTOR_LEFT_REAR] = wheel[3];
  targets[MOTOR_RIGHT_REAR] = wheel[2];
}

void Mecanum_Stop(void)
{
  Motor_ClosedLoopSetAllTarget(0);
}

void Mecanum_CalcWheelTargets(int32_t vx_cps,
                              int32_t vy_cps,
                              int32_t wz_cps,
                              int32_t targets[4])
{
  if (targets == NULL)
  {
    return;
  }

  /*
   * X layout based on the wheel force direction described above.
   *
   * Pure forward:
   *   M1 = M2 = M3 = M4 = +vx
   *
   * Pure left translation:
   *   M1/M4 reverse, M2/M3 forward
   *
   * Pure counterclockwise rotation:
   *   right side forward, left side reverse
   */
  targets[0] = vx_cps - vy_cps + wz_cps;  /* motor1: right-front */
  targets[1] = vx_cps + vy_cps - wz_cps;  /* motor2: left-front */
  targets[2] = vx_cps + vy_cps + wz_cps;  /* motor3: right-rear */
  targets[3] = vx_cps - vy_cps - wz_cps;  /* motor4: left-rear */
}

void Mecanum_NormalizeTargets(int32_t targets[4], int32_t limit_cps)
{
  int32_t max_abs = 0;

  if ((targets == NULL) || (limit_cps <= 0))
  {
    return;
  }

  for (uint8_t i = 0U; i < 4U; i++)
  {
    int32_t abs_value = Mecanum_Abs32(targets[i]);
    if (abs_value > max_abs)
    {
      max_abs = abs_value;
    }
  }

  if (max_abs <= limit_cps)
  {
    return;
  }

  for (uint8_t i = 0U; i < 4U; i++)
  {
    targets[i] = (targets[i] * limit_cps) / max_abs;
  }
}

static int32_t Mecanum_Abs32(int32_t value)
{
  return (value < 0) ? -value : value;
}



