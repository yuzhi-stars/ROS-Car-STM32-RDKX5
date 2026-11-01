/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Peripheral.h"
#include "OLED.h"
#include "IMU.h"
#include "imu_calibration.h"
#include "Vofa.h"
#include "Menu.h"
#include "LedStrip.h"
#include "Flash.h"
#include "Motor.h"
#include "INA219.h"
#include "Odom.h"
#include "ELRS.h"
#include "rdk_uart.h"
#include "runtime_clock.h"
#include "drive_control.h"
#include <stdio.h>
#include <math.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RAD_TO_DEG                     57.2957795f
#define ELRS_UNLOCK_BEEP_TIMES         3U
#define ELRS_UNLOCK_BEEP_ON_MS         100U
#define ELRS_UNLOCK_BEEP_GAP_MS        200U
#define ELRS_ALARM_BEEP_ON_MS          100U
#define ELRS_ALARM_BEEP_GAP_MS         100U
#define ELRS_ALARM_BLINK_MS            250U
#define ENCODER_COUNT_TEST_MODE        0U
#define VOFA_WIRELESS_ENABLE          0U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint32_t led_last_toggle_tick = 0;
static uint32_t led_strip_last_update_tick = 0;
static uint8_t led_is_on = 0;
static uint32_t imu_last_gyro_tick = 0;
#if VOFA_WIRELESS_ENABLE
static uint32_t vofa_last_send_tick = 0;
#endif
static uint32_t pid_save_last_tick = 0;
static uint32_t ina219_last_read_tick = 0;
static uint32_t elrs_alarm_last_toggle_tick = 0;
static uint8_t elrs_alarm_led_on = 0;
static uint8_t elrs_alarm_was_active = 0;
static IMU_AxisRaw_t gyro_raw;
static IMU_AxisRaw_t accel_raw;
static uint8_t imu_available = 0U;
static IMU_Calibration_t gyro_calibration;
static uint8_t gyro_bias_valid = 0U;
static float imu_yaw_deg = 0.0f;
static int32_t motor_encoder_speeds[MOTOR_COUNT] = {0};
#if VOFA_WIRELESS_ENABLE
static int32_t motor_target_speeds[MOTOR_COUNT] = {0};
#endif
static int32_t ina219_bus_voltage_mV = 0;
static int32_t ina219_current_mA = 0;
static Menu_Status_t menu_status;
volatile uint8_t g_peripherals_initialized = 0U;
static PID_Params_t motor_pid[4] =
{
  {0.00f, 0.10f, 0.00f},
  {0.00f, 0.10f, 0.00f},
  {0.00f, 0.10f, 0.00f},
  {0.00f, 0.10f, 0.00f}
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void App_CalibrateGyro(void);
static void App_CollectGyroCalibration(const IMU_AxisRaw_t *gyro,
    const IMU_AxisRaw_t *accel, uint8_t fresh, uint8_t may_calibrate);
static uint8_t App_MayCalibrateGyro(void);
static void App_PeripheralInit(void);
static void App_ELRSFeedbackProcess(uint32_t now_ms);
#if ENCODER_COUNT_TEST_MODE
static void App_EncoderCountTestProcess(uint32_t now_ms);
#endif

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void App_CalibrateGyro(void)
{
  IMU_AxisRaw_t gyro_sample = {0};
  IMU_AxisRaw_t accel_sample = {0};
  const uint16_t sample_count = 300U;

  OLED_ShowString(2, 0, "CAL IMU: STILL");
  gyro_bias_valid = 0U;
  IMU_Calibration_Init(&gyro_calibration);
  if (imu_available == 0U)
  {
    gyro_calibration.last_reason = IMU_CAL_SENSOR;
    return;
  }

  for (uint16_t i = 0; i < sample_count; i++)
  {
    uint8_t flags = 0U;
    (void)IMU_ReadMotionRaw(&gyro_sample, &accel_sample, &flags);
    App_CollectGyroCalibration(&gyro_sample, &accel_sample, flags, 1U);
    HAL_Delay(10);
  }
  gyro_bias_valid = gyro_calibration.valid;
  if (gyro_bias_valid == 0U) OLED_ShowString(2, 0, "CAL IMU: RETRY");

  imu_yaw_deg = 0.0f;
  imu_last_gyro_tick = HAL_GetTick();
}

static void App_CollectGyroCalibration(const IMU_AxisRaw_t *gyro,
    const IMU_AxisRaw_t *accel, uint8_t fresh, uint8_t may_calibrate)
{
  float raw[3] = {(float)gyro->x, (float)gyro->y, (float)gyro->z};
  float accel_g[3] = {(float)accel->x * 0.000061f,
                     (float)accel->y * 0.000061f, (float)accel->z * 0.000061f};
  (void)IMU_Calibration_Update(&gyro_calibration, raw, accel_g, fresh,
                              HAL_GetTick(), may_calibrate);
}

static uint8_t App_MayCalibrateGyro(void)
{
  int32_t targets[MOTOR_COUNT], speeds[MOTOR_COUNT];
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  Motor_ClosedLoopGetAllTargets(targets);
  Motor_GetAllEncoderSpeeds(speeds);
  __set_PRIMASK(mask);
  for (uint8_t i = 0U; i < MOTOR_COUNT; ++i)
    if (targets[i] != 0 || speeds[i] > 10 || speeds[i] < -10) return 0U;
  return 1U;
}



static void App_PeripheralInit(void)
{
  g_peripherals_initialized = 0U;

  /* Initialize PC13 user LED output state. */
  Peripheral_LED_Init();

  /* Initialize PC3 buzzer output state and rhythm player state. */
  Peripheral_BUZZER_Init();

#if ENCODER_COUNT_TEST_MODE
  /* Encoder count test mode: only OLED, motor PWM and encoder timers are enabled. */
  OLED_Init();
  Motor_Init();
  Motor_StopAll();
  Motor_ResetEncoderCounts();
  OLED_Clear();
  OLED_ShowString(0, 0, "ENC COUNTS TEST ");
  OLED_ShowString(6, 0, "RESET=REBOOT     ");
  g_peripherals_initialized = 1U;
  return;
#endif
  /* Initialize INA219 voltage/current sampling chip. */
  (void)INA219_Init();

  /* Initialize OLED display over I2C. */
  OLED_Init();

  /* Initialize LSM6DSR gyro/accelerometer and LIS3MDL magnetometer. */
  imu_available = (IMU_Init() == HAL_OK) ? 1U : 0U;

  /* Calibrate all gyro axes from new samples while stationary for about 3 s. */
  App_CalibrateGyro();

  /* Load PID parameters from MB85RC64T FRAM, or use defaults if invalid. */
  (void)Flash_PID_LoadOrDefault(motor_pid);

  /* Initialize four motor PWM outputs and hardware encoder timers. */
  Motor_Init();

  /* Initialize incremental PID closed-loop controller with PID table. */
  Motor_ClosedLoopInit(motor_pid);

  /* Initialize wheel odometry state from current encoder counts. */
  Odom_Init();

  /* Initialize all motor target speeds to zero. */
  Motor_ClosedLoopSetAllTarget(0);

  /* Configure menu status pointers for OLED display pages. */
  menu_status.yaw_deg = &imu_yaw_deg;
  menu_status.motor_encoder_speeds = motor_encoder_speeds;
  menu_status.ina219_bus_voltage_mV = &ina219_bus_voltage_mV;
  menu_status.ina219_current_mA = &ina219_current_mA;
  menu_status.imu_calibration = &gyro_calibration;

  /* Initialize OLED menu system and encoder-button state. */
  Menu_Init(motor_pid, &menu_status);

  /* Initialize WS2812B LED strip DMA driver. */
  LedStrip_Init();

  /* The shared timer commits exactly one selected drive source. */
  (void)Motor_ClosedLoopStart20ms();

  /* USART3 protocol owns its persistent RX/TX buffers and IT state. */
  (void)RDK_UART_Init(&huart3);
  if (gyro_bias_valid != 0U)
  {
    RDK_UART_SetGyroBiasRaw(gyro_calibration.bias_raw[0],
                          gyro_calibration.bias_raw[1], gyro_calibration.bias_raw[2]);
  }

  /* Initialize ELRS receiver interface on UART5 PC12/PD2. */
  ELRS_Init(&huart5);
  DriveControl_Init();


  /* Play startup buzzer prompt after all peripheral modules are configured. */
  Peripheral_BUZZER_Play(3U, 200U, 200U);

  /* Mark user peripheral initialization as complete. */
  g_peripherals_initialized = 1U;
}
static void App_ELRSFeedbackProcess(uint32_t now_ms)
{
  static uint8_t was_enabled = 0U;
  uint8_t alarm_active = ELRS_IsAlarmActive();
  uint8_t enabled = (uint8_t)(ELRS_IsLinkActive(now_ms) != 0U && DriveControl_IsEnabled() != 0U);
  (void)ELRS_ConsumeArmEvent();

  if ((enabled != 0U) && (was_enabled == 0U) && (alarm_active == 0U))
  {
    /* Announce the actual selected owner's permission, after stick centering. */
    Peripheral_BUZZER_Play(ELRS_UNLOCK_BEEP_TIMES,
                           ELRS_UNLOCK_BEEP_ON_MS,
                           ELRS_UNLOCK_BEEP_GAP_MS);
  }
  was_enabled = enabled;

  if (alarm_active != 0U)
  {
    Peripheral_BUZZER_AlarmOn(ELRS_ALARM_BEEP_ON_MS, ELRS_ALARM_BEEP_GAP_MS);

    if (((now_ms - elrs_alarm_last_toggle_tick) >= ELRS_ALARM_BLINK_MS) ||
        (elrs_alarm_was_active == 0U))
    {
      elrs_alarm_last_toggle_tick = now_ms;
      elrs_alarm_led_on = (uint8_t)!elrs_alarm_led_on;

      if (LedStrip_IsBusy() == 0U)
      {
        if (elrs_alarm_led_on != 0U)
        {
          LedStrip_Fill(80U, 0U, 0U);
        }
        else
        {
          LedStrip_Fill(0U, 0U, 0U);
        }
        (void)LedStrip_Show();
      }
    }

    elrs_alarm_was_active = 1U;
  }
  else
  {
    if (elrs_alarm_was_active != 0U)
    {
      Peripheral_BUZZER_AlarmOff();
      elrs_alarm_was_active = 0U;
      elrs_alarm_led_on = 0U;
    }
  }
}
#if ENCODER_COUNT_TEST_MODE
static void App_EncoderCountTestProcess(uint32_t now_ms)
{
  static uint32_t last_display_tick = 0U;
  int32_t counts[MOTOR_COUNT] = {0};
  char line[22];

  if ((now_ms - last_display_tick) < 200U)
  {
    return;
  }
  last_display_tick = now_ms;

  Motor_StopAll();
  Motor_ClosedLoopSetAllTarget(0);
  Motor_GetAllEncoderCounts(counts);

  OLED_ShowString(0, 0, "ENC COUNTS TEST ");
  snprintf(line, sizeof(line), "M1:%ld     ", (long)counts[0]);
  OLED_ShowString(1, 0, line);
  snprintf(line, sizeof(line), "M2:%ld     ", (long)counts[1]);
  OLED_ShowString(2, 0, line);
  snprintf(line, sizeof(line), "M3:%ld     ", (long)counts[2]);
  OLED_ShowString(3, 0, line);
  snprintf(line, sizeof(line), "M4:%ld     ", (long)counts[3]);
  OLED_ShowString(4, 0, line);
  OLED_ShowString(6, 0, "RESET=REBOOT     ");
}
#endif
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  RuntimeClock_Init();

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_TIM5_Init();
  MX_TIM13_Init();
  MX_TIM14_Init();
  MX_CAN1_Init();
  MX_I2C1_Init();
  MX_SPI2_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_TIM8_Init();
  MX_UART4_Init();
  MX_UART5_Init();
  /* USART1 is intentionally not initialized/armed in this build. */
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  App_PeripheralInit();
  /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    if (g_peripherals_initialized == 0U)
    {
      continue;
    }

#if ENCODER_COUNT_TEST_MODE
    App_EncoderCountTestProcess(now);
    continue;
#endif
    ELRS_Poll();
    DriveControl_Process();
    RDK_UART_PollRx();
    /* Read new 104 Hz sensor data at about 100 Hz; UART module runs Mahony
     * with the actual gyro sample interval and publishes the same quaternion. */
    if ((now - imu_last_gyro_tick) >= 10U)
    {
      imu_last_gyro_tick = now;
      uint8_t fresh_flags = 0U;
      if (imu_available != 0U)
      {
        (void)IMU_ReadMotionRaw(&gyro_raw, &accel_raw, &fresh_flags);
        if (gyro_bias_valid == 0U)
        {
          App_CollectGyroCalibration(&gyro_raw, &accel_raw, fresh_flags, App_MayCalibrateGyro());
          if (gyro_calibration.valid != 0U)
          {
            RDK_UART_SetGyroBiasRaw(gyro_calibration.bias_raw[0],
                                  gyro_calibration.bias_raw[1], gyro_calibration.bias_raw[2]);
            gyro_bias_valid = 1U;
          }
        }
      }
      RDK_UART_UpdateImuRaw(&gyro_raw, &accel_raw, fresh_flags);
      float q[4];
      if (RDK_UART_GetOrientation(q) != 0U)
      {
        /* Display/heading control and serial feedback now use one estimate. */
        imu_yaw_deg = atan2f(2.0f * (q[3] * q[2] + q[0] * q[1]),
                            1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2])) * RAD_TO_DEG;
      }
    }
    RDK_UART_Process(HAL_GetTick());
    App_ELRSFeedbackProcess(HAL_GetTick());

    if ((OLED_I2CAvailable() != 0U) && ((now - ina219_last_read_tick) >= 200U))
    {
      ina219_last_read_tick = now;
      (void)INA219_ReadBusVoltage_mV(&ina219_bus_voltage_mV);
      (void)INA219_ReadCurrent_mA(&ina219_current_mA);
    }

    /* LED strip: default rainbow flowing effect. */
    if ((ELRS_IsAlarmActive() == 0U) && ((now - led_strip_last_update_tick) >= 5U))
    {
      led_strip_last_update_tick = now;
      if (LedStrip_IsBusy() == 0U)
      {
        LedStrip_RainbowFlowStep();
      }
    }
    /* Read latest motor encoder speeds updated by the 20 ms closed-loop interrupt. */
    Motor_GetAllEncoderSpeeds(motor_encoder_speeds);

    /* Menu owns OLED display and refreshes changed pages. */
    Menu_Process(now);

#if VOFA_WIRELESS_ENABLE
    /* Send four motor current speeds and four target speeds to VOFA+ FireWater through UART4 wireless serial at 50 Hz. */
    if ((now - vofa_last_send_tick) >= 20U)
    {
      vofa_last_send_tick = now;
      Motor_ClosedLoopGetAllTargets(motor_target_speeds);
      (void)VOFA_SendMotorSpeedsAndTargets_DMA(&huart4, motor_encoder_speeds, motor_target_speeds);
    }
#endif

    /* Save PID parameters to MB85RC64T FRAM after menu changes. */
    if ((OLED_I2CAvailable() != 0U) && ((now - pid_save_last_tick) >= 1000U))
    {
      pid_save_last_tick = now;
      (void)Flash_PID_SaveIfChanged(motor_pid);
    }
    /* DMA refresh yields after every completion so other I2C clients can run. */
    OLED_Process(HAL_GetTick());

    if ((now - led_last_toggle_tick) >= 500U)
    {
      led_last_toggle_tick = now;
      led_is_on = !led_is_on;
      if (led_is_on)
      {
        LED_ON();
      }
      else
      {
        LED_OFF(); 
      }
    }
    /* Motor duty is now updated by the 20 ms closed-loop timer interrupt. */
}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
  HAL_RCC_MCOConfig(RCC_MCO1, RCC_MCO1SOURCE_LSE, RCC_MCODIV_1);
}

/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position)
{
  RDK_UART_RxEventCallback(huart, position);
  ELRS_RxEventCallback(huart, position);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  RDK_UART_TxCpltCallback(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  RDK_UART_ErrorCallback(huart);
  ELRS_ErrorCallback(huart);
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *i2c) { OLED_TxCpltCallback(i2c); }
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *i2c) { OLED_ErrorCallback(i2c); }
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *i2c) { OLED_ErrorCallback(i2c); }
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */












 



























































