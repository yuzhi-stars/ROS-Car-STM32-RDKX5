#include "Menu.h"
#include "OLED.h"
#include "Motor.h"
#include "rdk_uart.h"
#include "drive_control.h"
#include <stdio.h>

#define MENU_ENTER_DEBOUNCE_MS   50U
#define MENU_ENCODER_TRANSITIONS_PER_STEP 4
#define MENU_ENCODER_DEBOUNCE_MS 2U
#define MENU_STATUS_REDRAW_MS    100U
#define MENU_PID_MOTOR_COUNT     4U
#define MENU_PARAM_COUNT         4U
#define MENU_HOME_ITEM_COUNT     8U
#define MENU_PID_STEP            0.01f
#define MENU_PID_MIN             0.0f
#define MENU_PID_MAX             99.99f
#define MENU_TARGET_ITEM_COUNT    5U
#define MENU_TARGET_STEP          1000L
#define MENU_TARGET_MIN           -10000L
#define MENU_TARGET_MAX           10000L

typedef struct
{
  GPIO_PinState stable_state;
  GPIO_PinState last_sample;
  uint32_t last_change_tick;
  uint8_t press_latched;
} Menu_Button_t;

typedef enum
{
  MENU_PAGE_HOME = 0,
  MENU_PAGE_STATUS,
  MENU_PAGE_PID,
  MENU_PAGE_TARGET,
  MENU_PAGE_IMU
} Menu_Page_t;

static PID_Params_t *pid_table = NULL;
static Menu_Status_t *menu_status = NULL;
static Menu_Page_t current_page = MENU_PAGE_HOME;
static uint8_t home_cursor = 0U;
static uint8_t pid_motor_index = 0U;
static uint8_t pid_cursor = 0U;
static uint8_t pid_editing = 0U;
static uint8_t target_cursor = 0U;
static uint8_t target_editing = 0U;
static uint8_t redraw_request = 1U;
static uint32_t last_redraw_tick = 0U;
static uint8_t status_full_redraw = 1U;
static uint8_t screen_enabled = 0U;

static Menu_Button_t enter_key = {GPIO_PIN_SET, GPIO_PIN_SET, 0U, 0U};
static uint8_t encoder_stable_state = 0U;
static uint8_t encoder_last_sample_state = 0U;
static uint8_t encoder_last_decode_state = 0U;
static uint32_t encoder_last_change_tick = 0U;
static volatile int8_t encoder_pending_steps = 0;
static volatile uint8_t enter_pending = 0U;
static int8_t encoder_accumulator = 0;

static uint8_t Menu_EnterPressed(uint32_t now_ms);
static int8_t Menu_EncoderConsumeStep(void);
static void Menu_MoveCursor(int8_t dir);
static void Menu_Enter(void);
static void Menu_AdjustPID(int8_t dir);
static void Menu_AdjustTarget(int8_t dir);
static void Menu_Draw(void);
static void Menu_DrawHome(void);
static void Menu_DrawStatus(void);
static void Menu_DrawImu(void);
static void Menu_DrawPID(void);
static void Menu_DrawTarget(void);
static void Menu_DrawTargetLine(uint8_t index);
static void Menu_ShowLine(uint8_t page, const char *text, uint8_t highlight);
static void Menu_FormatPIDLine(char *buffer, uint16_t size, const char *name, float value);
static uint8_t Menu_ReadEncoderAB(void);
static int8_t Menu_DecodeTransition(uint8_t previous, uint8_t current);
static void Menu_ResetEncoderTracking(void);
static void Menu_ResetEnterTracking(void);

void Menu_Init(PID_Params_t *motor_pid_table, Menu_Status_t *status_ptr)
{
  pid_table = motor_pid_table;
  menu_status = status_ptr;
  current_page = MENU_PAGE_HOME;
  home_cursor = 0U;
  pid_motor_index = 0U;
  pid_cursor = 0U;
  pid_editing = 0U;
  target_cursor = 0U;
  target_editing = 0U;
  redraw_request = 1U;
  last_redraw_tick = 0U;

  enter_key.stable_state = HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin);
  enter_key.last_sample = enter_key.stable_state;
  enter_key.last_change_tick = HAL_GetTick();
  enter_key.press_latched = 0U;

  Menu_ResetEncoderTracking();
  screen_enabled = 0U;
  OLED_Clear();
  OLED_DisplayOff();
}

void Menu_Process(uint32_t now_ms)
{
  int8_t encoder_step = Menu_EncoderConsumeStep();
  uint8_t target_partial_redraw_done = 0U;

  if (screen_enabled == 0U)
  {
    if (Menu_EnterPressed(now_ms))
    {
      screen_enabled = 1U;
      current_page = MENU_PAGE_HOME;
      home_cursor = 0U;
      pid_editing = 0U;
      target_editing = 0U;
      status_full_redraw = 1U;
      Menu_ResetEncoderTracking();
      Menu_ResetEnterTracking();
      OLED_DisplayOn();
      OLED_Clear();
      redraw_request = 1U;
    }
    else
    {
      redraw_request = 0U;
    }
  }

  if ((screen_enabled != 0U) && (encoder_step != 0))
  {
    if ((current_page == MENU_PAGE_PID) && (pid_editing != 0U))
    {
      Menu_AdjustPID(encoder_step);
    }
    else if ((current_page == MENU_PAGE_TARGET) && (target_editing != 0U))
    {
      Menu_AdjustTarget(encoder_step);
      Menu_DrawTargetLine(target_cursor);
      target_partial_redraw_done = 1U;
    }
    else if (current_page == MENU_PAGE_TARGET)
    {
      uint8_t old_cursor = target_cursor;
      Menu_MoveCursor(encoder_step);
      Menu_DrawTargetLine(old_cursor);
      Menu_DrawTargetLine(target_cursor);
      target_partial_redraw_done = 1U;
    }
    else if ((current_page == MENU_PAGE_HOME) || (current_page == MENU_PAGE_PID) || (current_page == MENU_PAGE_TARGET))
    {
      Menu_MoveCursor(encoder_step);
    }
    if (target_partial_redraw_done == 0U)
    {
      redraw_request = 1U;
    }
  }

  if ((screen_enabled != 0U) && Menu_EnterPressed(now_ms))
  {
    Menu_Enter();
    redraw_request = 1U;
  }

  if ((screen_enabled != 0U) &&
      ((current_page == MENU_PAGE_STATUS) || (current_page == MENU_PAGE_IMU)) &&
      ((now_ms - last_redraw_tick) >= MENU_STATUS_REDRAW_MS))
  {
    redraw_request = 1U;
  }

  if ((screen_enabled != 0U) && (redraw_request != 0U))
  {
    redraw_request = 0U;
    last_redraw_tick = now_ms;
    Menu_Draw();
  }
}

uint8_t Menu_IsTargetControlActive(void)
{
  return ((screen_enabled != 0U) && (current_page == MENU_PAGE_TARGET)) ? 1U : 0U;
}

static uint8_t Menu_EnterPressed(uint32_t now_ms)
{
  uint8_t pressed = 0U;

  (void)now_ms;

  __disable_irq();
  if (enter_pending != 0U)
  {
    enter_pending = 0U;
    pressed = 1U;
  }
  __enable_irq();

  return pressed;
}

static uint8_t Menu_ReadEncoderAB(void)
{
  uint8_t a = (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  uint8_t b = (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  return (uint8_t)((a << 1U) | b);
}

static int8_t Menu_DecodeTransition(uint8_t previous, uint8_t current)
{
  uint8_t transition = (uint8_t)((previous << 2U) | current);

  switch (transition)
  {
    case 0x01: case 0x07: case 0x0E: case 0x08:
      return 1;
    case 0x02: case 0x0B: case 0x0D: case 0x04:
      return -1;
    default:
      return 0;
  }
}

static void Menu_ResetEncoderTracking(void)
{
  uint8_t current_state = Menu_ReadEncoderAB();

  __disable_irq();
  encoder_stable_state = current_state;
  encoder_last_sample_state = current_state;
  encoder_last_decode_state = current_state;
  encoder_last_change_tick = HAL_GetTick();
  encoder_pending_steps = 0;
  encoder_accumulator = 0;
  __enable_irq();
}

static void Menu_ResetEnterTracking(void)
{
  __disable_irq();
  enter_key.stable_state = HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin);
  enter_key.last_sample = enter_key.stable_state;
  enter_key.last_change_tick = HAL_GetTick();
  enter_key.press_latched = (enter_key.stable_state == GPIO_PIN_RESET) ? 1U : 0U;
  enter_pending = 0U;
  __enable_irq();
}

static int8_t Menu_EncoderConsumeStep(void)
{
  int8_t step = 0;

  __disable_irq();
  if (encoder_pending_steps > 0)
  {
    encoder_pending_steps--;
    step = 1;
  }
  else if (encoder_pending_steps < 0)
  {
    encoder_pending_steps++;
    step = -1;
  }
  __enable_irq();

  return step;
}

static void Menu_MoveCursor(int8_t dir)
{
  uint8_t count = MENU_HOME_ITEM_COUNT;
  uint8_t *cursor = &home_cursor;

  if (current_page == MENU_PAGE_PID)
  {
    count = MENU_PARAM_COUNT;
    cursor = &pid_cursor;
  }
  else if (current_page == MENU_PAGE_TARGET)
  {
    count = MENU_TARGET_ITEM_COUNT;
    cursor = &target_cursor;
  }

  if (dir > 0)
  {
    *cursor = (uint8_t)((*cursor + 1U) % count);
  }
  else
  {
    *cursor = (uint8_t)((*cursor == 0U) ? (count - 1U) : (*cursor - 1U));
  }
}

static void Menu_Enter(void)
{
  if (current_page == MENU_PAGE_HOME)
  {
    if (home_cursor == 0U)
    {
      current_page = MENU_PAGE_STATUS;
      status_full_redraw = 1U;
    }
    else if (home_cursor <= 4U)
    {
      pid_motor_index = (uint8_t)(home_cursor - 1U);
      pid_cursor = 0U;
      pid_editing = 0U;
      current_page = MENU_PAGE_PID;
    }
    else if (home_cursor == 5U)
    {
      target_cursor = 0U;
      target_editing = 0U;
      current_page = MENU_PAGE_TARGET;
    }
    else if (home_cursor == 6U)
    {
      current_page = MENU_PAGE_IMU;
    }
    else
    {
      screen_enabled = 0U;
      OLED_Clear();
      OLED_DisplayOff();
    }
  }
  else if ((current_page == MENU_PAGE_STATUS) || (current_page == MENU_PAGE_IMU))
  {
    current_page = MENU_PAGE_HOME;
  }
  else if (current_page == MENU_PAGE_PID)
  {
    if (pid_cursor == 3U)
    {
      pid_editing = 0U;
      current_page = MENU_PAGE_HOME;
    }
    else
    {
      pid_editing = (uint8_t)!pid_editing;
    }
  }
  else
  {
    if (target_cursor == 4U)
    {
      target_editing = 0U;
      current_page = MENU_PAGE_HOME;
    }
    else
    {
      target_editing = (uint8_t)!target_editing;
    }
  }

  Menu_ResetEncoderTracking();
  Menu_ResetEnterTracking();
}

static void Menu_AdjustPID(int8_t dir)
{
  float *target = NULL;

  if ((pid_table == NULL) || (pid_motor_index >= MENU_PID_MOTOR_COUNT))
  {
    return;
  }

  if (pid_cursor == 0U) target = &pid_table[pid_motor_index].kp;
  else if (pid_cursor == 1U) target = &pid_table[pid_motor_index].ki;
  else if (pid_cursor == 2U) target = &pid_table[pid_motor_index].kd;
  else return;

  *target += ((dir > 0) ? MENU_PID_STEP : -MENU_PID_STEP);
  if (*target < MENU_PID_MIN) *target = MENU_PID_MIN;
  if (*target > MENU_PID_MAX) *target = MENU_PID_MAX;
}

static void Menu_AdjustTarget(int8_t dir)
{
  int32_t targets[MOTOR_COUNT];
  int32_t target;

  /* Runtime owner controls targets; this page is read-only in the normal build. */
  if (RDK_UART_CONTROL_ENABLE != 0U) return;

  if (target_cursor >= MOTOR_COUNT)
  {
    return;
  }

  Motor_ClosedLoopGetAllTargets(targets);
  target = targets[target_cursor];
  target += (dir > 0) ? MENU_TARGET_STEP : -MENU_TARGET_STEP;

  if (target > MENU_TARGET_MAX) target = MENU_TARGET_MAX;
  if (target < MENU_TARGET_MIN) target = MENU_TARGET_MIN;

  Motor_ClosedLoopSetTarget((Motor_ID_t)target_cursor, target);
}

static void Menu_Draw(void)
{
  if (current_page == MENU_PAGE_HOME)
  {
    Menu_DrawHome();
  }
  else if (current_page == MENU_PAGE_STATUS)
  {
    Menu_DrawStatus();
  }
  else if (current_page == MENU_PAGE_PID)
  {
    Menu_DrawPID();
  }
  else if (current_page == MENU_PAGE_IMU)
  {
    Menu_DrawImu();
  }
  else
  {
    Menu_DrawTarget();
  }
}

static void Menu_DrawHome(void)
{
  Menu_ShowLine(0, "STATUS", (home_cursor == 0U));
  Menu_ShowLine(1, "M1 PID", (home_cursor == 1U));
  Menu_ShowLine(2, "M2 PID", (home_cursor == 2U));
  Menu_ShowLine(3, "M3 PID", (home_cursor == 3U));
  Menu_ShowLine(4, "M4 PID", (home_cursor == 4U));
  Menu_ShowLine(5, "TARGET", (home_cursor == 5U));
  Menu_ShowLine(6, "IMU DIAG", (home_cursor == 6U));
  Menu_ShowLine(7, "DISPLAY OFF", (home_cursor == 7U));
}

static void Menu_DrawImu(void)
{
  char line[24];
  RDK_UART_ImuDiagnostics_t d;
  RDK_UART_GetImuDiagnostics(&d);
  const IMU_Calibration_t *cal = menu_status != NULL ? menu_status->imu_calibration : NULL;
  const char *state = d.bias_valid == 0U ? "IMU:WAIT CAL" :
      (d.orientation_valid != 0U ? "IMU:READY" : (d.initialized != 0U ? "IMU:STALE" : "IMU:ALIGN"));
  Menu_ShowLine(0, state, 0U);
  Menu_ShowLine(1, d.bias_valid != 0U ? "CAL:OK" : "CAL:WAIT STILL", 0U);
  static const char *const reasons[] = {"OK", "NO SENSOR", "WHEELS MOVE", "ACCEL STALE",
      "GRAVITY", "GYRO MOVING", "GYRO GAP", "GYRO NOISE"};
  unsigned int reason = cal != NULL ? (unsigned int)cal->last_reason : (unsigned int)IMU_CAL_SENSOR;
  if (reason >= sizeof(reasons)/sizeof(reasons[0])) reason = IMU_CAL_SENSOR;
  snprintf(line, sizeof(line), "WHY:%s", reasons[reason]);
  Menu_ShowLine(2, line, 0U);
  snprintf(line, sizeof(line), "CAL N:%u R:%lu", cal != NULL ? (unsigned int)cal->samples : 0U,
      (unsigned long)(cal != NULL ? cal->rejected_windows : 0U));
  Menu_ShowLine(3, line, 0U);
  snprintf(line, sizeof(line), "ALIGN:%u/32 R:%lu", (unsigned int)d.alignment_samples,
      (unsigned long)d.alignment_resets);
  Menu_ShowLine(4, line, 0U);
  if (d.gyro_age_ms == UINT32_MAX || d.accel_age_ms == UINT32_MAX)
    snprintf(line, sizeof(line), "AGE: NO SAMPLES");
  else
    snprintf(line, sizeof(line), "AGE G:%lu A:%lu MS",
        (unsigned long)(d.gyro_age_ms > 9999U ? 9999U : d.gyro_age_ms),
        (unsigned long)(d.accel_age_ms > 9999U ? 9999U : d.accel_age_ms));
  Menu_ShowLine(5, line, 0U);
  snprintf(line, sizeof(line), "GAP:%lu MAX:%lu MS", (unsigned long)d.gyro_gap_resets,
      (unsigned long)(d.max_gyro_gap_ms > 9999U ? 9999U : d.max_gyro_gap_ms));
  Menu_ShowLine(6, line, 0U);
  Menu_ShowLine(7, "PRESS TO RETURN", 0U);
}

static void Menu_DrawStatus(void)
{
  char line[18];
  int32_t yaw_cdeg = 0;
  int32_t enc0 = 0;
  int32_t enc1 = 0;
  int32_t enc2 = 0;
  int32_t enc3 = 0;

  OLED_ClearLine(0);
  OLED_ShowString(0, 0, DriveControl_GetLabel());
  status_full_redraw = 0U;

  if ((menu_status != NULL) && (menu_status->yaw_deg != NULL))
  {
    yaw_cdeg = (int32_t)(*menu_status->yaw_deg * 100.0f);
  }
  snprintf(line, sizeof(line), "YAW:%ld.%02ld", (long)(yaw_cdeg / 100), (long)((yaw_cdeg < 0 ? -yaw_cdeg : yaw_cdeg) % 100));
  OLED_ClearLine(1);
  OLED_ShowString(1, 0, line);

  if ((menu_status != NULL) && (menu_status->ina219_bus_voltage_mV != NULL))
  {
    int32_t mv = *menu_status->ina219_bus_voltage_mV;
    snprintf(line, sizeof(line), "V:%ld.%02ld", (long)(mv / 1000L), (long)((mv >= 0 ? mv : -mv) % 1000L) / 10L);
  }
  else
  {
    snprintf(line, sizeof(line), "V:--");
  }
  OLED_ClearLine(2);
  OLED_ShowString(2, 0, line);

  if ((menu_status != NULL) && (menu_status->ina219_current_mA != NULL))
  {
    int32_t ma = *menu_status->ina219_current_mA;
    int32_t abs_ma = (ma >= 0) ? ma : -ma;

    if (abs_ma >= 1000L)
    {
      snprintf(line, sizeof(line), "I:%s%ld.%03ldA",
               (ma < 0) ? "-" : "",
               (long)(abs_ma / 1000L),
               (long)(abs_ma % 1000L));
    }
    else
    {
      snprintf(line, sizeof(line), "I:%ldMA", (long)ma);
    }
  }
  else
  {
    snprintf(line, sizeof(line), "I:--");
  }
  OLED_ClearLine(3);
  OLED_ShowString(3, 0, line);

  if ((menu_status != NULL) && (menu_status->motor_encoder_speeds != NULL))
  {
    enc0 = menu_status->motor_encoder_speeds[0];
    enc1 = menu_status->motor_encoder_speeds[1];
    enc2 = menu_status->motor_encoder_speeds[2];
    enc3 = menu_status->motor_encoder_speeds[3];
  }

  snprintf(line, sizeof(line), "M1:%ld", (long)enc0);
  OLED_ClearLine(4);
  OLED_ShowString(4, 0, line);
  snprintf(line, sizeof(line), "M2:%ld", (long)enc1);
  OLED_ClearLine(5);
  OLED_ShowString(5, 0, line);
  snprintf(line, sizeof(line), "M3:%ld", (long)enc2);
  OLED_ClearLine(6);
  OLED_ShowString(6, 0, line);
  snprintf(line, sizeof(line), "M4:%ld", (long)enc3);
  OLED_ClearLine(7);
  OLED_ShowString(7, 0, line);
}

static void Menu_DrawPID(void)
{
  char line[18];

  OLED_ClearLine(0);
  snprintf(line, sizeof(line), "M%u PID", (unsigned)(pid_motor_index + 1U));
  OLED_ShowString(0, 0, line);

  if (pid_table == NULL)
  {
    Menu_ShowLine(2, "NO PID", 1U);
    return;
  }

  Menu_FormatPIDLine(line, sizeof(line), "KP", pid_table[pid_motor_index].kp);
  Menu_ShowLine(1, line, (pid_cursor == 0U));
  Menu_FormatPIDLine(line, sizeof(line), "KI", pid_table[pid_motor_index].ki);
  Menu_ShowLine(2, line, (pid_cursor == 1U));
  Menu_FormatPIDLine(line, sizeof(line), "KD", pid_table[pid_motor_index].kd);
  Menu_ShowLine(3, line, (pid_cursor == 2U));
  Menu_ShowLine(4, pid_editing ? "EDIT" : "BACK", (pid_cursor == 3U));
  OLED_ClearLine(5);
  OLED_ClearLine(6);
  OLED_ClearLine(7);
}

static void Menu_DrawTarget(void)
{
  for (uint8_t i = 0U; i < MOTOR_COUNT; i++)
  {
    Menu_DrawTargetLine(i);
  }

  Menu_DrawTargetLine(4U);
  OLED_ClearLine(5);
  OLED_ClearLine(6);
  OLED_ClearLine(7);
}

static void Menu_DrawTargetLine(uint8_t index)
{
  char line[22];
  int32_t targets[MOTOR_COUNT] = {0};
  int32_t current = 0;

  if (index < MOTOR_COUNT)
  {
    Motor_ClosedLoopGetAllTargets(targets);

    if ((menu_status != NULL) && (menu_status->motor_encoder_speeds != NULL))
    {
      current = menu_status->motor_encoder_speeds[index];
    }

    snprintf(line,
             sizeof(line),
             "M%u T%ld C%ld",
             (unsigned)(index + 1U),
             (long)targets[index],
             (long)current);
    Menu_ShowLine(index, line, (target_cursor == index));
  }
  else if (index == 4U)
  {
    Menu_ShowLine(4, target_editing ? "EDIT" : "BACK", (target_cursor == 4U));
  }
}
static void Menu_ShowLine(uint8_t page, const char *text, uint8_t highlight)
{
  OLED_ClearLine(page);
  if (highlight != 0U)
  {
    OLED_ShowStringInverted(page, 0, text);
  }
  else
  {
    OLED_ShowString(page, 0, text);
  }
}

static void Menu_FormatPIDLine(char *buffer, uint16_t size, const char *name, float value)
{
  int32_t cvalue = (int32_t)(value * 100.0f);
  if (cvalue < 0)
  {
    cvalue = 0;
  }
  snprintf(buffer, size, "%s:%ld.%02ld", name, (long)(cvalue / 100), (long)(cvalue % 100));
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint8_t current_state;
  int8_t delta;
  uint32_t now_ms;

  if (g_peripherals_initialized == 0U)
  {
    return;
  }

  now_ms = HAL_GetTick();

  if (GPIO_Pin == KEY3_Pin)
  {
    if (((now_ms - enter_key.last_change_tick) >= MENU_ENTER_DEBOUNCE_MS) &&
        (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET))
    {
      enter_key.last_change_tick = now_ms;
      enter_pending = 1U;
    }
    return;
  }

  if ((GPIO_Pin != KEY1_Pin) && (GPIO_Pin != KEY2_Pin))
  {
    return;
  }
  if ((now_ms - encoder_last_change_tick) < MENU_ENCODER_DEBOUNCE_MS)
  {
    return;
  }
  encoder_last_change_tick = now_ms;

  current_state = Menu_ReadEncoderAB();
  if (current_state == encoder_last_decode_state)
  {
    return;
  }

  delta = Menu_DecodeTransition(encoder_last_decode_state, current_state);
  encoder_last_decode_state = current_state;

  if (delta == 0)
  {
    encoder_accumulator = 0;
    return;
  }

  if (((encoder_accumulator > 0) && (delta < 0)) ||
      ((encoder_accumulator < 0) && (delta > 0)))
  {
    encoder_accumulator = 0;
  }

  encoder_accumulator += delta;
  if (encoder_accumulator >= MENU_ENCODER_TRANSITIONS_PER_STEP)
  {
    encoder_accumulator = 0;
    if (encoder_pending_steps < 10)
    {
      encoder_pending_steps++;
    }
  }
  else if (encoder_accumulator <= -MENU_ENCODER_TRANSITIONS_PER_STEP)
  {
    encoder_accumulator = 0;
    if (encoder_pending_steps > -10)
    {
      encoder_pending_steps--;
    }
  }
}


