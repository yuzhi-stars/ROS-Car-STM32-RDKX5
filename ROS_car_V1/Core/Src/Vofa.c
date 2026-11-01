#include "Vofa.h"
#include <stdio.h>

static char vofa_dma_tx_buffer[160];

HAL_StatusTypeDef VOFA_SendMotorSpeeds(UART_HandleTypeDef *huart,
                                       const int32_t speeds[VOFA_MOTOR_SPEED_COUNT])
{
  char tx_buffer[80];
  int len;

  if ((huart == NULL) || (speeds == NULL))
  {
    return HAL_ERROR;
  }

  /*
   * VOFA+ FireWater format:
   *   <name>:ch0,ch1,ch2,ch3\n
   *
   * Channels:
   *   ch0 = motor 1 encoder speed, counts/s
   *   ch1 = motor 2 encoder speed, counts/s
   *   ch2 = motor 3 encoder speed, counts/s
   *   ch3 = motor 4 encoder speed, counts/s
   */
  len = snprintf(tx_buffer,
                 sizeof(tx_buffer),
                 "motor:%ld,%ld,%ld,%ld\n",
                 (long)speeds[0],
                 (long)speeds[1],
                 (long)speeds[2],
                 (long)speeds[3]);

  if ((len <= 0) || (len >= (int)sizeof(tx_buffer)))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(huart, (uint8_t *)tx_buffer, (uint16_t)len, 10);
}
HAL_StatusTypeDef VOFA_SendMotorSpeedsAndTargets(UART_HandleTypeDef *huart,
                                                 const int32_t speeds[VOFA_MOTOR_SPEED_COUNT],
                                                 const int32_t targets[VOFA_MOTOR_SPEED_COUNT])
{
  char tx_buffer[160];
  int len;

  if ((huart == NULL) || (speeds == NULL) || (targets == NULL))
  {
    return HAL_ERROR;
  }

  /*
   * VOFA+ FireWater channels:
   *   ch0~ch3 = motor 1~4 current encoder speed, counts/s
   *   ch4~ch7 = motor 1~4 target encoder speed, counts/s
   */
  len = snprintf(tx_buffer,
                 sizeof(tx_buffer),
                 "motor:%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                 (long)speeds[0],
                 (long)speeds[1],
                 (long)speeds[2],
                 (long)speeds[3],
                 (long)targets[0],
                 (long)targets[1],
                 (long)targets[2],
                 (long)targets[3]);

  if ((len <= 0) || (len >= (int)sizeof(tx_buffer)))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(huart, (uint8_t *)tx_buffer, (uint16_t)len, 10);
}
HAL_StatusTypeDef VOFA_SendMotorSpeedsAndTargets_DMA(UART_HandleTypeDef *huart,
                                                     const int32_t speeds[VOFA_MOTOR_SPEED_COUNT],
                                                     const int32_t targets[VOFA_MOTOR_SPEED_COUNT])
{
  int len;

  if ((huart == NULL) || (speeds == NULL) || (targets == NULL))
  {
    return HAL_ERROR;
  }

  if (huart->gState != HAL_UART_STATE_READY)
  {
    return HAL_BUSY;
  }

  len = snprintf(vofa_dma_tx_buffer,
                 sizeof(vofa_dma_tx_buffer),
                 "motor:%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
                 (long)speeds[0],
                 (long)speeds[1],
                 (long)speeds[2],
                 (long)speeds[3],
                 (long)targets[0],
                 (long)targets[1],
                 (long)targets[2],
                 (long)targets[3]);

  if ((len <= 0) || (len >= (int)sizeof(vofa_dma_tx_buffer)))
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit_DMA(huart, (uint8_t *)vofa_dma_tx_buffer, (uint16_t)len);
}

