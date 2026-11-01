/**
 * @file    VoiceNavigation.c
 * @brief   Voice navigation commands over STM32F405 hardware I2C1.
 *
 * Wiring:
 *   PB8 -> I2C1_SCL
 *   PB9 -> I2C1_SDA
 *
 * I2C device:
 *   7-bit address: 0x2B
 *   result register: 0x64
 */

#include "VoiceNavigation.h"
#include "i2c.h"

#define VOICE_I2C_ADDRESS_7BIT       0x2BU
#define VOICE_I2C_ADDRESS_HAL        (VOICE_I2C_ADDRESS_7BIT << 1U)
#define VOICE_RESULT_REGISTER        0x64U

#define VOICE_CMD_NAVIGATE_POINT_1   0x13U
#define VOICE_CMD_NAVIGATE_POINT_2   0x14U
#define VOICE_CMD_NAVIGATE_POINT_3   0x15U
#define VOICE_CMD_RETURN_ORIGIN      0x21U

#define VOICE_I2C_TIMEOUT_MS         10U

/* Square vertices used by the current 1 m square path, excluding origin. */
#define NAV_POINT_1_X_M              1.0f
#define NAV_POINT_1_Y_M              0.0f
#define NAV_POINT_2_X_M              1.0f
#define NAV_POINT_2_Y_M              1.0f
#define NAV_POINT_3_X_M              0.0f
#define NAV_POINT_3_Y_M              1.0f

volatile uint8_t g_navigation_target = NAVIGATION_TARGET_NONE;

/* Prevent a held result register from triggering repeatedly. */
static uint8_t s_last_voice_command = 0xFFU;

HAL_StatusTypeDef VoiceNavigation_CheckDevice(void)
{
  return HAL_I2C_IsDeviceReady(&hi2c1,
                               VOICE_I2C_ADDRESS_HAL,
                               3U,
                               VOICE_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef VoiceNavigation_ReadCommand(uint8_t *command)
{
  if (command == NULL)
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Mem_Read(&hi2c1,
                          VOICE_I2C_ADDRESS_HAL,
                          VOICE_RESULT_REGISTER,
                          I2C_MEMADD_SIZE_8BIT,
                          command,
                          1U,
                          VOICE_I2C_TIMEOUT_MS);
}

HAL_StatusTypeDef VoiceNavigation_Poll(void)
{
  uint8_t command = 0U;
  HAL_StatusTypeDef status;

  status = VoiceNavigation_ReadCommand(&command);
  if (status != HAL_OK)
  {
    return status;
  }

  if (command == s_last_voice_command)
  {
    return HAL_OK;
  }

  s_last_voice_command = command;

  switch (command)
  {
    case VOICE_CMD_NAVIGATE_POINT_1:
      g_navigation_target = NAVIGATION_TARGET_POINT_1;
      break;

    case VOICE_CMD_NAVIGATE_POINT_2:
      g_navigation_target = NAVIGATION_TARGET_POINT_2;
      break;

    case VOICE_CMD_NAVIGATE_POINT_3:
      g_navigation_target = NAVIGATION_TARGET_POINT_3;
      break;

    case VOICE_CMD_RETURN_ORIGIN:
      g_navigation_target = NAVIGATION_TARGET_ORIGIN;
      break;

    default:
      break;
  }

  return HAL_OK;
}

void VoiceNavigation_ClearTarget(void)
{
  g_navigation_target = NAVIGATION_TARGET_NONE;
}

uint8_t VoiceNavigation_GetTargetPoint(uint8_t target, Navigation_Point_t *point)
{
  if (point == NULL)
  {
    return 0U;
  }

  switch (target)
  {
    case NAVIGATION_TARGET_ORIGIN:
      point->x_m = 0.0f;
      point->y_m = 0.0f;
      return 1U;

    case NAVIGATION_TARGET_POINT_1:
      point->x_m = NAV_POINT_1_X_M;
      point->y_m = NAV_POINT_1_Y_M;
      return 1U;

    case NAVIGATION_TARGET_POINT_2:
      point->x_m = NAV_POINT_2_X_M;
      point->y_m = NAV_POINT_2_Y_M;
      return 1U;

    case NAVIGATION_TARGET_POINT_3:
      point->x_m = NAV_POINT_3_X_M;
      point->y_m = NAV_POINT_3_Y_M;
      return 1U;

    default:
      return 0U;
  }
}
