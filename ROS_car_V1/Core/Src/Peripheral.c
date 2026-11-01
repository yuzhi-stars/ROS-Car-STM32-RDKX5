#include "Peripheral.h"

#define BUZZER_TICK_MS 20U

typedef enum
{
  BUZZER_PHASE_OFF = 0,
  BUZZER_PHASE_ON
} Buzzer_Phase_t;

typedef struct
{
  volatile uint8_t active;
  volatile Buzzer_Phase_t phase;
  volatile uint16_t remaining_times;
  volatile uint16_t on_ticks;
  volatile uint16_t interval_ticks;
  volatile uint16_t elapsed_ticks;
} Buzzer_Player_t;

static Buzzer_Player_t buzzer_player = {0};
static volatile uint8_t buzzer_alarm_force = 0U;
static volatile uint16_t buzzer_alarm_on_ticks = 1U;
static volatile uint16_t buzzer_alarm_interval_ticks = 1U;
static volatile uint16_t buzzer_alarm_elapsed_ticks = 0U;
static volatile uint8_t buzzer_alarm_phase_on = 1U;
static volatile uint8_t buzzer_force_on = 0U;

static uint16_t Peripheral_BUZZER_MsToTicks(uint16_t ms);

void Peripheral_LED_Init(void)
{
  LED_OFF();
}

void Peripheral_LED_Blink_1s(void)
{
  LED_ON();
  HAL_Delay(500);
  LED_OFF();
  HAL_Delay(500);
}

void Peripheral_BUZZER_Init(void)
{
  __disable_irq();
  buzzer_player.active = 0U;
  buzzer_player.phase = BUZZER_PHASE_OFF;
  buzzer_player.remaining_times = 0U;
  buzzer_player.on_ticks = 0U;
  buzzer_player.interval_ticks = 0U;
  buzzer_player.elapsed_ticks = 0U;
  buzzer_alarm_force = 0U;
  buzzer_alarm_elapsed_ticks = 0U;
  buzzer_alarm_phase_on = 1U;
  buzzer_force_on = 0U;
  __enable_irq();

  BUZZER_OFF();
}

void Peripheral_BUZZER_Beep(uint32_t duration_ms)
{
  BUZZER_ON();
  HAL_Delay(duration_ms);
  BUZZER_OFF();
}

void Peripheral_LED_BUZZER_Blink_1s(void)
{
  LED_ON();
  BUZZER_ON();
  HAL_Delay(500);
  LED_OFF();
  BUZZER_OFF();
  HAL_Delay(500);
}

void Peripheral_BUZZER_Play(uint16_t times,
                            uint16_t on_ms,
                            uint16_t interval_ms)
{
  uint16_t on_ticks;
  uint16_t interval_ticks;

  if ((times == 0U) || (on_ms == 0U))
  {
    Peripheral_BUZZER_Stop();
    return;
  }

  on_ticks = Peripheral_BUZZER_MsToTicks(on_ms);
  interval_ticks = Peripheral_BUZZER_MsToTicks(interval_ms);

  __disable_irq();
  buzzer_alarm_force = 0U;
  buzzer_player.active = 1U;
  buzzer_player.phase = BUZZER_PHASE_ON;
  buzzer_player.remaining_times = times;
  buzzer_player.on_ticks = on_ticks;
  buzzer_player.interval_ticks = interval_ticks;
  buzzer_player.elapsed_ticks = 0U;
  __enable_irq();

  BUZZER_ON();
}

void Peripheral_BUZZER_Stop(void)
{
  __disable_irq();
  buzzer_force_on = 0U;
  buzzer_alarm_force = 0U;
  buzzer_player.active = 0U;
  buzzer_player.phase = BUZZER_PHASE_OFF;
  buzzer_player.remaining_times = 0U;
  buzzer_player.elapsed_ticks = 0U;
  buzzer_alarm_elapsed_ticks = 0U;
  buzzer_alarm_phase_on = 1U;
  __enable_irq();

  BUZZER_OFF();
}

void Peripheral_BUZZER_ForceOn(void)
{
  __disable_irq();
  buzzer_player.active = 0U;
  buzzer_alarm_force = 0U;
  buzzer_alarm_elapsed_ticks = 0U;
  buzzer_alarm_phase_on = 1U;
  buzzer_force_on = 1U;
  __enable_irq();

  BUZZER_ON();
}

void Peripheral_BUZZER_AlarmOn(uint16_t on_ms, uint16_t interval_ms)
{
  uint16_t on_ticks = Peripheral_BUZZER_MsToTicks(on_ms);
  uint16_t interval_ticks = Peripheral_BUZZER_MsToTicks(interval_ms);

  if (on_ticks == 0U)
  {
    on_ticks = 1U;
  }

  __disable_irq();
  buzzer_force_on = 0U;
  buzzer_player.active = 0U;
  buzzer_alarm_force = 1U;
  buzzer_alarm_on_ticks = on_ticks;
  buzzer_alarm_interval_ticks = interval_ticks;
  __enable_irq();
}

void Peripheral_BUZZER_AlarmOff(void)
{
  __disable_irq();
  buzzer_force_on = 0U;
  buzzer_alarm_force = 0U;
  buzzer_alarm_elapsed_ticks = 0U;
  buzzer_alarm_phase_on = 1U;
  __enable_irq();

  if (buzzer_player.active == 0U)
  {
    BUZZER_OFF();
  }
}

uint8_t Peripheral_BUZZER_IsBusy(void)
{
  return buzzer_player.active;
}

void Peripheral_BUZZER_Tick20ms(void)
{
  if (buzzer_force_on != 0U)
  {
    BUZZER_ON();
    return;
  }

  if (buzzer_alarm_force != 0U)
  {
    if (buzzer_alarm_elapsed_ticks < 0xFFFFU)
    {
      buzzer_alarm_elapsed_ticks++;
    }

    if (buzzer_alarm_phase_on != 0U)
    {
      BUZZER_ON();
      if (buzzer_alarm_elapsed_ticks >= buzzer_alarm_on_ticks)
      {
        buzzer_alarm_elapsed_ticks = 0U;
        buzzer_alarm_phase_on = 0U;
        BUZZER_OFF();
      }
    }
    else
    {
      BUZZER_OFF();
      if (buzzer_alarm_elapsed_ticks >= buzzer_alarm_interval_ticks)
      {
        buzzer_alarm_elapsed_ticks = 0U;
        buzzer_alarm_phase_on = 1U;
        BUZZER_ON();
      }
    }
    return;
  }

  if (buzzer_player.active == 0U)
  {
    BUZZER_OFF();
    return;
  }

  if (buzzer_player.elapsed_ticks < 0xFFFFU)
  {
    buzzer_player.elapsed_ticks++;
  }

  if (buzzer_player.phase == BUZZER_PHASE_ON)
  {
    BUZZER_ON();

    if (buzzer_player.elapsed_ticks >= buzzer_player.on_ticks)
    {
      BUZZER_OFF();
      buzzer_player.elapsed_ticks = 0U;

      if (buzzer_player.remaining_times > 0U)
      {
        buzzer_player.remaining_times--;
      }

      if (buzzer_player.remaining_times == 0U)
      {
        buzzer_player.active = 0U;
      }
      else if (buzzer_player.interval_ticks == 0U)
      {
        buzzer_player.phase = BUZZER_PHASE_ON;
        BUZZER_ON();
      }
      else
      {
        buzzer_player.phase = BUZZER_PHASE_OFF;
      }
    }
  }
  else
  {
    BUZZER_OFF();

    if (buzzer_player.elapsed_ticks >= buzzer_player.interval_ticks)
    {
      buzzer_player.elapsed_ticks = 0U;
      buzzer_player.phase = BUZZER_PHASE_ON;
      BUZZER_ON();
    }
  }
}

static uint16_t Peripheral_BUZZER_MsToTicks(uint16_t ms)
{
  if (ms == 0U)
  {
    return 0U;
  }

  return (uint16_t)((ms + BUZZER_TICK_MS - 1U) / BUZZER_TICK_MS);
}
