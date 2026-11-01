#include "Odom.h"
#include "Motor.h"
#include <math.h>

#define ODOM_UPDATE_DT_S              0.020f
#define ODOM_WHEEL_DIAMETER_M         0.0800f
#define ODOM_WHEEL_CIRCUMFERENCE_M    0.251327412f
#define ODOM_COUNTS_PER_REV           1333.0f
#define ODOM_METER_PER_COUNT          (ODOM_WHEEL_CIRCUMFERENCE_M / ODOM_COUNTS_PER_REV)
#define ODOM_COUNT_DIRECTION_SIGN      1.0f

#define ODOM_HALF_LENGTH_M            0.0835f
#define ODOM_HALF_WIDTH_M             0.0940f
#define ODOM_K_M                      (ODOM_HALF_LENGTH_M + ODOM_HALF_WIDTH_M)

static Odom_State_t odom_state = {0};
static int32_t odom_last_counts[MOTOR_COUNT] = {0};
static uint8_t odom_initialized = 0U;

static float Odom_NormalizeYaw(float yaw_rad);

static uint32_t Odom_Lock(void)
{
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  return mask;
}

void Odom_Init(void)
{
  uint32_t mask = Odom_Lock();
  Odom_Reset();
  odom_initialized = 1U;
  __set_PRIMASK(mask);
}

void Odom_Reset(void)
{
  int32_t counts[MOTOR_COUNT] = {0};

  uint32_t mask = Odom_Lock();
  Motor_GetAllEncoderCounts(counts);
  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    odom_last_counts[i] = counts[i];
  }

  odom_state.x_m = 0.0f;
  odom_state.y_m = 0.0f;
  odom_state.yaw_rad = 0.0f;
  odom_state.vx_mps = 0.0f;
  odom_state.vy_mps = 0.0f;
  odom_state.wz_radps = 0.0f;
  __set_PRIMASK(mask);
}

void Odom_Update20ms(void)
{
  int32_t counts[MOTOR_COUNT] = {0};
  int32_t delta[MOTOR_COUNT] = {0};
  float d1_m;
  float d2_m;
  float d3_m;
  float d4_m;
  float dx_body;
  float dy_body;
  float dyaw;
  float cos_yaw;
  float sin_yaw;

  if (odom_initialized == 0U)
  {
    Odom_Init();
    return;
  }

  Motor_GetAllEncoderCounts(counts);

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    delta[i] = (int32_t)(int16_t)(counts[i] - odom_last_counts[i]);
    odom_last_counts[i] = counts[i];
  }

  /*
   * Existing Motor_ID_t order:
   *   MOTOR_LEFT_FRONT, MOTOR_RIGHT_FRONT, MOTOR_LEFT_REAR, MOTOR_RIGHT_REAR
   * User physical order for mecanum model:
   *   motor1 = right-front, motor2 = left-front, motor3 = right-rear, motor4 = left-rear
   */
  d1_m = (float)delta[MOTOR_RIGHT_FRONT] * ODOM_METER_PER_COUNT * ODOM_COUNT_DIRECTION_SIGN;
  d2_m = (float)delta[MOTOR_LEFT_FRONT]  * ODOM_METER_PER_COUNT * ODOM_COUNT_DIRECTION_SIGN;
  d3_m = (float)delta[MOTOR_RIGHT_REAR]  * ODOM_METER_PER_COUNT * ODOM_COUNT_DIRECTION_SIGN;
  d4_m = (float)delta[MOTOR_LEFT_REAR]   * ODOM_METER_PER_COUNT * ODOM_COUNT_DIRECTION_SIGN;

  dx_body = (d1_m + d2_m + d3_m + d4_m) * 0.25f;
  dy_body = (-d1_m + d2_m + d3_m - d4_m) * 0.25f;
  dyaw = (d1_m - d2_m + d3_m - d4_m) / (4.0f * ODOM_K_M);

  float dt_s = Motor_GetEncoderSamplePeriodS();
  if (!(dt_s > 0.0f)) dt_s = ODOM_UPDATE_DT_S;
  odom_state.vx_mps = dx_body / dt_s;
  odom_state.vy_mps = dy_body / dt_s;
  odom_state.wz_radps = dyaw / dt_s;

  cos_yaw = cosf(odom_state.yaw_rad);
  sin_yaw = sinf(odom_state.yaw_rad);

  odom_state.x_m += dx_body * cos_yaw - dy_body * sin_yaw;
  odom_state.y_m += dx_body * sin_yaw + dy_body * cos_yaw;
  odom_state.yaw_rad = Odom_NormalizeYaw(odom_state.yaw_rad + dyaw);
}

void Odom_GetState(Odom_State_t *state)
{
  if (state == NULL)
  {
    return;
  }

  uint32_t mask = Odom_Lock();
  *state = odom_state;
  __set_PRIMASK(mask);
}

void Odom_GetPose(float *x_m, float *y_m, float *yaw_rad)
{
  Odom_State_t snapshot;
  Odom_GetState(&snapshot);
  if (x_m != NULL)
  {
    *x_m = snapshot.x_m;
  }
  if (y_m != NULL)
  {
    *y_m = snapshot.y_m;
  }
  if (yaw_rad != NULL)
  {
    *yaw_rad = snapshot.yaw_rad;
  }
}

void Odom_SetPose(float x_m, float y_m, float yaw_rad)
{
  int32_t counts[MOTOR_COUNT] = {0};
  uint32_t primask;

  if (!isfinite(x_m) || !isfinite(y_m) || !isfinite(yaw_rad)) return;
  /* Reduction must stay outside the critical section, even for huge inputs. */
  float normalized_yaw = Odom_NormalizeYaw(yaw_rad);

  primask = __get_PRIMASK();
  __disable_irq();
  Motor_GetAllEncoderCounts(counts);

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    odom_last_counts[i] = counts[i];
  }

  odom_state.x_m = x_m;
  odom_state.y_m = y_m;
  odom_state.yaw_rad = normalized_yaw;
  odom_state.vx_mps = 0.0f;
  odom_state.vy_mps = 0.0f;
  odom_state.wz_radps = 0.0f;

  __set_PRIMASK(primask);
}

void Odom_GetVelocity(float *vx_mps, float *vy_mps, float *wz_radps)
{
  Odom_State_t snapshot;
  Odom_GetState(&snapshot);
  if (vx_mps != NULL)
  {
    *vx_mps = snapshot.vx_mps;
  }
  if (vy_mps != NULL)
  {
    *vy_mps = snapshot.vy_mps;
  }
  if (wz_radps != NULL)
  {
    *wz_radps = snapshot.wz_radps;
  }
}

static float Odom_NormalizeYaw(float yaw_rad)
{
  const float pi = 3.141592654f;
  const float two_pi = 6.283185307f;

  if ((yaw_rad > pi) || (yaw_rad < -pi)) yaw_rad = fmodf(yaw_rad, two_pi);
  if (yaw_rad > pi)
  {
    yaw_rad -= two_pi;
  }
  if (yaw_rad < -pi)
  {
    yaw_rad += two_pi;
  }

  return yaw_rad;
}
