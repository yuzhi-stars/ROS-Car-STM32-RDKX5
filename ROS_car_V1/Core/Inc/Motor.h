#ifndef __MOTOR_H
#define __MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "Menu.h"

#define MOTOR_COUNT 4U
#define MOTOR_DUTY_MAX 10000

typedef enum
{
  MOTOR_LEFT_FRONT = 0,
  MOTOR_RIGHT_FRONT,
  MOTOR_LEFT_REAR,
  MOTOR_RIGHT_REAR
} Motor_ID_t;

typedef struct
{
  float target;
  float feedback;
  float error;
  float last_error;
  float prev_error;
  float output;
} Motor_PID_State_t;

void Motor_Init(void);
void Motor_StopAll(void);
void Motor_SetDuty(Motor_ID_t motor, int16_t duty);
void Motor_ClosedLoopInit(const PID_Params_t pid[MOTOR_COUNT]);
void Motor_ClosedLoopSetTarget(Motor_ID_t motor, int32_t target_cps);
void Motor_ClosedLoopSetAllTarget(int32_t target_cps);
/* Clear targets and accumulated PID output without stopping the shared timer.
 * Caller must exclude the motor-control interrupt while calling this function. */
void Motor_ClosedLoopEmergencyStop(void);
void Motor_ClosedLoopGetAllTargets(int32_t targets[MOTOR_COUNT]);
HAL_StatusTypeDef Motor_ClosedLoopStart20ms(void);
HAL_StatusTypeDef Motor_ControlTimerStart20ms(void);
void Motor_ClosedLoopStop(void);
void Motor_ClosedLoopIRQHandler(void);

int32_t Motor_GetEncoderCount(Motor_ID_t motor);
void Motor_GetAllEncoderCounts(int32_t counts[MOTOR_COUNT]);
void Motor_ResetEncoderCounts(void);
void Motor_UpdateEncoderSpeed(uint32_t now_ms);
int32_t Motor_GetEncoderSpeed(Motor_ID_t motor);
void Motor_GetAllEncoderSpeeds(int32_t speeds[MOTOR_COUNT]);
uint32_t Motor_GetEncoderSampleTick(void);
float Motor_GetEncoderSamplePeriodS(void);
float Motor_IncrementalPID_Update(Motor_ID_t motor, float target, float feedback, const PID_Params_t pid[MOTOR_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H */









