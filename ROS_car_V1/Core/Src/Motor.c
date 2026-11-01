#include "Motor.h"
#include "tim.h"
#include "gpio.h"
#include "runtime_clock.h"
#include "motor_speed_filter.h"

#define MOTOR_PWM_PERIOD 4199U

typedef struct
{
  TIM_HandleTypeDef *pwm_timer;
  uint32_t pwm_channel;
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin;
  TIM_HandleTypeDef *encoder_timer;
  int8_t output_sign;
  int8_t direction_sign;
} Motor_Config_t;

static const Motor_Config_t motor_config[MOTOR_COUNT] =
{
  /* ML1: use original ML2 wiring, PA3 TIM5_CH4, PB1 DIR, encoder R2 TIM2 PA15/PB3. */
  {&htim5, TIM_CHANNEL_4, GPIOB, GPIO_PIN_1, &htim2,  -1, 1},
  /* MR1: use previous ML2 wiring, PA1 TIM5_CH2, PA5 DIR, encoder R1 TIM3 PB4/PB5. */
  {&htim5, TIM_CHANNEL_2, GPIOA, GPIO_PIN_5, &htim3, 1,  -1},
  /* ML2: PA2 TIM5_CH3, PC4 DIR, encoder TIM8 PC6/PC7. */
  {&htim5, TIM_CHANNEL_3, GPIOC, GPIO_PIN_4, &htim8,  -1,  1},
  /* MR2: PA0 TIM5_CH1, PA4 DIR, encoder L1 TIM4 PB6/PB7. */
  {&htim5, TIM_CHANNEL_1, GPIOA, GPIO_PIN_4, &htim4,  1, -1}
};

static Motor_PID_State_t pid_state[MOTOR_COUNT];
static int32_t encoder_last_count[MOTOR_COUNT] = {0};
static int32_t encoder_speed_cps[MOTOR_COUNT] = {0};
static float encoder_speed_lpf_cps[MOTOR_COUNT] = {0};
static uint32_t encoder_last_update_tick = 0U;
static uint32_t encoder_last_cycles = 0U;
static float encoder_sample_period_s = 0.020f;
static const PID_Params_t *closed_loop_pid = NULL;
static int32_t closed_loop_target_cps[MOTOR_COUNT] = {0, 0, 0, 0};
static uint8_t closed_loop_enabled = 0U;

static uint32_t Motor_Abs16(int16_t value);
static void Motor_SetRawPwm(Motor_ID_t motor, int16_t pwm);
static void Motor_SetCompare(Motor_ID_t motor, uint32_t compare);

void Motor_Init(void)
{
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_4);

  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim8, TIM_CHANNEL_ALL);

  __HAL_TIM_SET_COUNTER(&htim2, 0U);
  __HAL_TIM_SET_COUNTER(&htim3, 0U);
  __HAL_TIM_SET_COUNTER(&htim4, 0U);
  __HAL_TIM_SET_COUNTER(&htim8, 0U);

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    pid_state[i].target = 0.0f;
    pid_state[i].feedback = 0.0f;
    pid_state[i].error = 0.0f;
    pid_state[i].last_error = 0.0f;
    pid_state[i].prev_error = 0.0f;
    pid_state[i].output = 0.0f;
    encoder_last_count[i] = Motor_GetEncoderCount((Motor_ID_t)i);
    encoder_speed_cps[i] = 0;
    encoder_speed_lpf_cps[i] = 0;
  }
  encoder_last_update_tick = HAL_GetTick();
  encoder_last_cycles = RuntimeClock_Cycles();

  Motor_StopAll();
}

void Motor_StopAll(void)
{
  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    Motor_SetDuty((Motor_ID_t)i, 0);
  }
}

static void Motor_SetRawPwm(Motor_ID_t motor, int16_t pwm)
{
  uint32_t duty;
  uint32_t compare;
  GPIO_PinState dir_state;

  if (motor >= MOTOR_COUNT)
  {
    return;
  }

  if (pwm == 0)
  {
    /* DRV8870: IN1=0, IN2=0 -> coast/stop. */
    HAL_GPIO_WritePin(motor_config[motor].dir_port, motor_config[motor].dir_pin, GPIO_PIN_RESET);
    Motor_SetCompare(motor, 0U);
    return;
  }

  duty = Motor_Abs16(pwm);
  if (duty > MOTOR_PWM_PERIOD)
  {
    duty = MOTOR_PWM_PERIOD;
  }

  if (pwm > 0)
  {
    /* DRV8701E forward: PH/DIR=0, EN/PWM=duty. */
    dir_state = GPIO_PIN_RESET;
    compare = duty;
  }
  else
  {
    /* DRV8701E reverse: PH/DIR=1, EN/PWM=duty. Do not invert PWM. */
    dir_state = GPIO_PIN_SET;
    compare = duty;
  }

  HAL_GPIO_WritePin(motor_config[motor].dir_port, motor_config[motor].dir_pin, dir_state);
  Motor_SetCompare(motor, compare);
}

void Motor_SetDuty(Motor_ID_t motor, int16_t duty)
{
  int32_t pwm;

  if (motor >= MOTOR_COUNT)
  {
    return;
  }

  if (duty > MOTOR_DUTY_MAX)
  {
    duty = MOTOR_DUTY_MAX;
  }
  else if (duty < -MOTOR_DUTY_MAX)
  {
    duty = -MOTOR_DUTY_MAX;
  }

  pwm = ((int32_t)duty * (int32_t)MOTOR_PWM_PERIOD) / MOTOR_DUTY_MAX;
  pwm *= (int32_t)motor_config[motor].output_sign;
  Motor_SetRawPwm(motor, (int16_t)pwm);
}

void Motor_ClosedLoopInit(const PID_Params_t pid[MOTOR_COUNT])
{
  closed_loop_pid = pid;
  closed_loop_enabled = 0U;

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    pid_state[i].target = 0.0f;
    pid_state[i].feedback = 0.0f;
    pid_state[i].error = 0.0f;
    pid_state[i].last_error = 0.0f;
    pid_state[i].prev_error = 0.0f;
    pid_state[i].output = 0.0f;
  }
}

void Motor_ClosedLoopSetTarget(Motor_ID_t motor, int32_t target_cps)
{
  if (motor >= MOTOR_COUNT)
  {
    return;
  }

  closed_loop_target_cps[motor] = target_cps;
}

void Motor_ClosedLoopSetAllTarget(int32_t target_cps)
{
  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    closed_loop_target_cps[i] = target_cps;
  }
}

void Motor_ClosedLoopGetAllTargets(int32_t targets[MOTOR_COUNT])
{
  if (targets == NULL)
  {
    return;
  }

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    targets[i] = closed_loop_target_cps[i];
  }
}

void Motor_ClosedLoopEmergencyStop(void)
{
  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    closed_loop_target_cps[i] = 0;
    pid_state[i].target = 0.0f;
    pid_state[i].error = 0.0f;
    pid_state[i].last_error = 0.0f;
    pid_state[i].prev_error = 0.0f;
    pid_state[i].output = 0.0f;
  }
  Motor_StopAll();
}

HAL_StatusTypeDef Motor_ControlTimerStart20ms(void)
{
  /* TIM13 is configured here as a 20 ms shared control timer.
   * APB1 timer clock is 84 MHz in the current clock tree.
   * 84 MHz / (8399 + 1) = 10 kHz, 200 ticks = 20 ms.
   */
  HAL_TIM_Base_Stop_IT(&htim13);
  __HAL_TIM_SET_PRESCALER(&htim13, 8399U);
  __HAL_TIM_SET_AUTORELOAD(&htim13, 199U);
  __HAL_TIM_SET_COUNTER(&htim13, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim13, TIM_FLAG_UPDATE);

  HAL_NVIC_SetPriority(TIM8_UP_TIM13_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(TIM8_UP_TIM13_IRQn);

  return HAL_TIM_Base_Start_IT(&htim13);
}

HAL_StatusTypeDef Motor_ClosedLoopStart20ms(void)
{
  if (closed_loop_pid == NULL)
  {
    return HAL_ERROR;
  }

  closed_loop_enabled = 1U;
  return Motor_ControlTimerStart20ms();
}

void Motor_ClosedLoopStop(void)
{
  closed_loop_enabled = 0U;
  HAL_TIM_Base_Stop_IT(&htim13);
  Motor_StopAll();
}

void Motor_ClosedLoopIRQHandler(void)
{
  Motor_UpdateEncoderSpeed(HAL_GetTick());

  if ((closed_loop_enabled == 0U) || (closed_loop_pid == NULL))
  {
    return;
  }

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    Motor_ID_t motor = (Motor_ID_t)i;
    int32_t feedback_cps = Motor_GetEncoderSpeed(motor);
    float output = Motor_IncrementalPID_Update(motor,
                                               (float)closed_loop_target_cps[i],
                                               (float)feedback_cps,
                                               closed_loop_pid);
    Motor_SetDuty(motor, (int16_t)output);
  }
}




int32_t Motor_GetEncoderCount(Motor_ID_t motor)
{
  int32_t count;

  if (motor >= MOTOR_COUNT)
  {
    return 0;
  }

  count = (int32_t)(int16_t)__HAL_TIM_GET_COUNTER(motor_config[motor].encoder_timer);
  return count * (int32_t)motor_config[motor].direction_sign;
}

void Motor_GetAllEncoderCounts(int32_t counts[MOTOR_COUNT])
{
  if (counts == NULL)
  {
    return;
  }

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    counts[i] = Motor_GetEncoderCount((Motor_ID_t)i);
  }
}
void Motor_ResetEncoderCounts(void)
{
  __HAL_TIM_SET_COUNTER(&htim2, 0U);
  __HAL_TIM_SET_COUNTER(&htim3, 0U);
  __HAL_TIM_SET_COUNTER(&htim4, 0U);
  __HAL_TIM_SET_COUNTER(&htim8, 0U);

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    encoder_last_count[i] = 0;
    encoder_speed_cps[i] = 0;
    encoder_speed_lpf_cps[i] = 0;
    pid_state[i].output = 0.0f;
  }

  encoder_last_update_tick = HAL_GetTick();
  encoder_last_cycles = RuntimeClock_Cycles();
}
void Motor_UpdateEncoderSpeed(uint32_t now_ms)
{
  uint32_t cycles = RuntimeClock_Cycles();
  uint32_t elapsed = (uint32_t)(cycles - encoder_last_cycles);
  if (elapsed == 0U) return;
  encoder_sample_period_s = RuntimeClock_Seconds(elapsed);
  float counts_per_second = 1.0f / encoder_sample_period_s;

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    int32_t current_count = Motor_GetEncoderCount((Motor_ID_t)i);
    int32_t delta_count = (int32_t)(int16_t)(current_count - encoder_last_count[i]);
    float raw_speed_cps = (float)delta_count * counts_per_second;
    encoder_speed_lpf_cps[i] = MotorSpeedFilter_Update(encoder_speed_lpf_cps[i], raw_speed_cps);
    encoder_speed_cps[i] = (int32_t)lroundf(encoder_speed_lpf_cps[i]);
    encoder_last_count[i] = current_count;
  }

  encoder_last_update_tick = now_ms;
  encoder_last_cycles = cycles;
}

int32_t Motor_GetEncoderSpeed(Motor_ID_t motor)
{
  if (motor >= MOTOR_COUNT)
  {
    return 0;
  }

  return encoder_speed_cps[motor];
}

uint32_t Motor_GetEncoderSampleTick(void)
{
  return encoder_last_update_tick;
}

float Motor_GetEncoderSamplePeriodS(void) { return encoder_sample_period_s; }

void Motor_GetAllEncoderSpeeds(int32_t speeds[MOTOR_COUNT])
{
  if (speeds == NULL)
  {
    return;
  }

  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    speeds[i] = Motor_GetEncoderSpeed((Motor_ID_t)i);
  }
}
float Motor_IncrementalPID_Update(Motor_ID_t motor, float target, float feedback, const PID_Params_t pid[MOTOR_COUNT])
{
  float delta;

  if ((motor >= MOTOR_COUNT) || (pid == NULL))
  {
    return 0.0f;
  }

  pid_state[motor].target = target;
  pid_state[motor].feedback = feedback;
  pid_state[motor].error = target - feedback;

  delta = pid[motor].kp * (pid_state[motor].error - pid_state[motor].last_error) +
          pid[motor].ki * pid_state[motor].error +
          pid[motor].kd * (pid_state[motor].error - 2.0f * pid_state[motor].last_error + pid_state[motor].prev_error);

  pid_state[motor].output += delta;

  if (pid_state[motor].output > (float)MOTOR_DUTY_MAX)
  {
    pid_state[motor].output = (float)MOTOR_DUTY_MAX;
  }
  else if (pid_state[motor].output < -(float)MOTOR_DUTY_MAX)
  {
    pid_state[motor].output = -(float)MOTOR_DUTY_MAX;
  }

  pid_state[motor].prev_error = pid_state[motor].last_error;
  pid_state[motor].last_error = pid_state[motor].error;

  return pid_state[motor].output;
}

static uint32_t Motor_Abs16(int16_t value)
{
  return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static void Motor_SetCompare(Motor_ID_t motor, uint32_t compare)
{
  __HAL_TIM_SET_COMPARE(motor_config[motor].pwm_timer, motor_config[motor].pwm_channel, compare);
}






























