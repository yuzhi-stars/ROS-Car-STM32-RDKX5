#ifndef RDK_UART_TEST_MOCK_HAL_H
#define RDK_UART_TEST_MOCK_HAL_H

/* Suppress device-specific headers for the native protocol/behavior tests.
 * The real headers are compiled separately by the firmware build. */
#define __MAIN_H
#define __IMU_H
#define __MOTOR_H
#define __I2C_H__
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_RX 1U
#define HAL_UART_STATE_BUSY_TX 2U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_ORE 8U
#define HAL_UART_ERROR_DMA 16U
typedef struct { uint32_t CR; } DMA_Stream_TypeDef;
typedef struct { uint32_t remaining; DMA_Stream_TypeDef *Instance; uint32_t State; } DMA_HandleTypeDef;
#define DMA_SxCR_EN 1U
#define HAL_DMA_STATE_READY 0U
#define HAL_DMA_STATE_BUSY 1U
#define HAL_DMA_STATE_TIMEOUT 2U
typedef struct
{
    uint32_t RxState;
    uint32_t gState;
    uint32_t ErrorCode;
    DMA_HandleTypeDef *hdmarx;
    DMA_HandleTypeDef *hdmatx;
} UART_HandleTypeDef;
typedef struct { int16_t x, y, z; } IMU_AxisRaw_t;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t unused; } GPIO_TypeDef;
#define KEY1_Pin 1U
#define KEY2_Pin 2U
#define KEY3_Pin 4U
#define KEY1_GPIO_Port ((GPIO_TypeDef *)(uintptr_t)1U)
#define KEY2_GPIO_Port KEY1_GPIO_Port
#define KEY3_GPIO_Port KEY1_GPIO_Port
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
extern volatile uint8_t g_peripherals_initialized;
typedef struct { uint32_t State, busy; } I2C_HandleTypeDef;
extern I2C_HandleTypeDef hi2c1;
#define HAL_I2C_STATE_READY 0U
#define HAL_I2C_STATE_BUSY_TX 1U
#define I2C_FLAG_BUSY 1U
#define RESET 0U
#define __HAL_I2C_GET_FLAG(i2c, flag) ((i2c)->busy)
uint32_t HAL_I2C_GetState(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *, uint16_t, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *, uint16_t);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *);
void HAL_Delay(uint32_t);
#define IMU_MOTION_ACCEL_FRESH 0x01U
#define IMU_MOTION_GYRO_FRESH 0x02U
typedef enum
{
    MOTOR_LEFT_FRONT, MOTOR_RIGHT_FRONT, MOTOR_LEFT_REAR, MOTOR_RIGHT_REAR
} Motor_ID_t;
#define MOTOR_COUNT 4U
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->remaining)
#define __DMB() ((void)0)
uint32_t RuntimeClock_Cycles(void);
uint8_t RuntimeClock_BudgetExpired(uint32_t, uint32_t);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
#define __enable_irq() __set_PRIMASK(0U)
#define __HAL_UART_CLEAR_OREFLAG(uart) ((uart)->ErrorCode = HAL_UART_ERROR_NONE)
void Motor_ClosedLoopEmergencyStop(void);
void Motor_ClosedLoopSetTarget(Motor_ID_t, int32_t);
void Motor_GetAllEncoderSpeeds(int32_t speeds[4]);
void Motor_ClosedLoopGetAllTargets(int32_t targets[4]);
void Motor_ClosedLoopSetAllTarget(int32_t);
void Motor_GetAllEncoderCounts(int32_t counts[4]);
float Motor_GetEncoderSamplePeriodS(void);
uint32_t Motor_GetEncoderSampleTick(void);
#ifdef __cplusplus
}
#endif
#endif
