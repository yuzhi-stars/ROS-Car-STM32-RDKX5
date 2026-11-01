#ifndef __VOFA_H
#define __VOFA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "usart.h"

#define VOFA_MOTOR_SPEED_COUNT 4U
#define VOFA_MOTOR_CHANNEL_COUNT 8U

HAL_StatusTypeDef VOFA_SendMotorSpeeds(UART_HandleTypeDef *huart,
                                       const int32_t speeds[VOFA_MOTOR_SPEED_COUNT]);
HAL_StatusTypeDef VOFA_SendMotorSpeedsAndTargets(UART_HandleTypeDef *huart,
                                                 const int32_t speeds[VOFA_MOTOR_SPEED_COUNT],
                                                 const int32_t targets[VOFA_MOTOR_SPEED_COUNT]);
HAL_StatusTypeDef VOFA_SendMotorSpeedsAndTargets_DMA(UART_HandleTypeDef *huart,
                                                     const int32_t speeds[VOFA_MOTOR_SPEED_COUNT],
                                                     const int32_t targets[VOFA_MOTOR_SPEED_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* __VOFA_H */
