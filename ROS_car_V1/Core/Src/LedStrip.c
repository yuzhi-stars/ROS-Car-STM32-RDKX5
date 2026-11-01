#include "LedStrip.h"
#include "tim.h"
#include <string.h>

/*
 * XL-5050RGBC-WS2812B / WS2812B compatible single-wire LED driver.
 * Output pin: PB0 / TIM1_CH2N.
 * Timer: TIM1, 800 kHz PWM. Current TIM1 period is 209 ticks.
 */
#define LED_STRIP_BITS_PER_LED     24U
#define LED_STRIP_RESET_SLOTS      300U
#define LED_STRIP_PWM_0            50U
#define LED_STRIP_PWM_1            110U
#define LED_STRIP_BUFFER_SIZE      ((LED_STRIP_LED_COUNT * LED_STRIP_BITS_PER_LED) + LED_STRIP_RESET_SLOTS)

static LedStrip_Color_t led_pixels[LED_STRIP_LED_COUNT];
static uint32_t led_pwm_buffer[LED_STRIP_BUFFER_SIZE];
static volatile uint8_t led_strip_busy = 0U;
static uint32_t led_strip_start_tick = 0U;


static void LedStrip_EncodeBuffer(void);
static void LedStrip_PutByte(uint32_t *buffer, uint8_t value);
static void LedStrip_ColorWheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b);

void LedStrip_Init(void)
{
  __HAL_TIM_MOE_ENABLE(&htim1);
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);
  led_strip_busy = 0U;

  LedStrip_Fill(0, 0, 0);
  (void)LedStrip_Show();
}

void LedStrip_SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index >= LED_STRIP_LED_COUNT)
  {
    return;
  }

  led_pixels[index].r = r;
  led_pixels[index].g = g;
  led_pixels[index].b = b;
}

void LedStrip_Fill(uint8_t r, uint8_t g, uint8_t b)
{
  for (uint16_t i = 0U; i < LED_STRIP_LED_COUNT; i++)
  {
    LedStrip_SetPixel(i, r, g, b);
  }
}

HAL_StatusTypeDef LedStrip_Show(void)
{
  HAL_StatusTypeDef status;

  if (led_strip_busy != 0U)
  {
    if ((HAL_GetTick() - led_strip_start_tick) < 10U)
    {
      return HAL_BUSY;
    }
    HAL_TIMEx_PWMN_Stop_DMA(&htim1, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);
    led_strip_busy = 0U;
  }

  HAL_TIMEx_PWMN_Stop_DMA(&htim1, TIM_CHANNEL_2);
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);

  LedStrip_EncodeBuffer();
  led_strip_start_tick = HAL_GetTick();
  led_strip_busy = 1U;

  status = HAL_TIMEx_PWMN_Start_DMA(&htim1,
                                    TIM_CHANNEL_2,
                                    led_pwm_buffer,
                                    LED_STRIP_BUFFER_SIZE);
  if (status != HAL_OK)
  {
    led_strip_busy = 0U;
  }

  return status;
}

uint8_t LedStrip_IsBusy(void)
{
  if ((led_strip_busy != 0U) && ((HAL_GetTick() - led_strip_start_tick) > 10U))
  {
    HAL_TIMEx_PWMN_Stop_DMA(&htim1, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0U);
    led_strip_busy = 0U;
  }

  return led_strip_busy;
}

void LedStrip_TestRainbowStep(void)
{
  static uint8_t step = 0U;

  switch (step % 6U)
  {
    case 0U: LedStrip_Fill(32, 0, 0); break;
    case 1U: LedStrip_Fill(0, 32, 0); break;
    case 2U: LedStrip_Fill(0, 0, 32); break;
    case 3U: LedStrip_Fill(32, 32, 0); break;
    case 4U: LedStrip_Fill(0, 32, 32); break;
    default: LedStrip_Fill(32, 0, 32); break;
  }

  step++;
  (void)LedStrip_Show();
}

void LedStrip_AlternateGroupsRainbowStep(void)
{
  static uint8_t active_left_group = 1U;
  static uint8_t color_pos = 0U;
  uint8_t r;
  uint8_t g;
  uint8_t b;

  LedStrip_ColorWheel(color_pos, &r, &g, &b);
  color_pos = (uint8_t)(color_pos + 16U);

  LedStrip_Fill(0, 0, 0);

  if (active_left_group != 0U)
  {
    /* Left group: LED 1-5 and LED 11-15, using 0-based indexes 0-4 and 10-14. */
    for (uint16_t i = 0U; i < 5U; i++)
    {
      LedStrip_SetPixel(i, r, g, b);
    }
    for (uint16_t i = 10U; i < 15U; i++)
    {
      LedStrip_SetPixel(i, r, g, b);
    }
  }
  else
  {
    /* Right group: LED 6-10 and LED 16-20, using 0-based indexes 5-9 and 15-19. */
    for (uint16_t i = 5U; i < 10U; i++)
    {
      LedStrip_SetPixel(i, r, g, b);
    }
    for (uint16_t i = 15U; i < LED_STRIP_LED_COUNT; i++)
    {
      LedStrip_SetPixel(i, r, g, b);
    }
  }

  active_left_group = (uint8_t)!active_left_group;
  (void)LedStrip_Show();
}


void LedStrip_BreathStep(void)
{
  static uint8_t brightness = 0U;
  static int8_t direction = 1;

  /* Soft blue breathing light. Brightness is limited to reduce current for 20 LEDs. */
  LedStrip_Fill(0U, 0U, brightness);
  (void)LedStrip_Show();

  if (direction > 0)
  {
    if (brightness >= 160U)
    {
      direction = -1;
    }
    else
    {
      brightness = (uint8_t)(brightness + 10U);
    }
  }
  else
  {
    if (brightness <= 10U)
    {
      brightness = 0U;
      direction = 1;
    }
    else
    {
      brightness = (uint8_t)(brightness - 10U);
    }
  }
}
void LedStrip_RainbowFlowStep(void)
{
  static uint8_t flow_offset = 0U;
  uint8_t r;
  uint8_t g;
  uint8_t b;

  /*
   * Physical grouping:
   *   group 1  = LED 1  and LED 11
   *   group 2  = LED 2  and LED 12
   *   ...
   *   group 10 = LED 10 and LED 20
   * Flow direction starts from group 1 and moves to group 10.
   */
  for (uint16_t group = 0U; group < 10U; group++)
  {
    uint8_t color_pos = (uint8_t)(flow_offset - (uint8_t)(group * 16U));
    LedStrip_ColorWheel(color_pos, &r, &g, &b);

    LedStrip_SetPixel(group, r, g, b);
    LedStrip_SetPixel((uint16_t)(group + 10U), r, g, b);
  }

  flow_offset = (uint8_t)(flow_offset + 4U);
  (void)LedStrip_Show();
}
static void LedStrip_EncodeBuffer(void)
{
  uint16_t pos = 0U;

  memset(led_pwm_buffer, 0, sizeof(led_pwm_buffer));

  for (uint16_t i = 0U; i < LED_STRIP_LED_COUNT; i++)
  {
    /* WS2812 data order is GRB. */
    LedStrip_PutByte(&led_pwm_buffer[pos], led_pixels[i].g);
    pos = (uint16_t)(pos + 8U);
    LedStrip_PutByte(&led_pwm_buffer[pos], led_pixels[i].r);
    pos = (uint16_t)(pos + 8U);
    LedStrip_PutByte(&led_pwm_buffer[pos], led_pixels[i].b);
    pos = (uint16_t)(pos + 8U);
  }
}

static void LedStrip_PutByte(uint32_t *buffer, uint8_t value)
{
  for (uint8_t bit = 0U; bit < 8U; bit++)
  {
    buffer[bit] = ((value & (uint8_t)(0x80U >> bit)) != 0U) ? LED_STRIP_PWM_1 : LED_STRIP_PWM_0;
  }
}

static void LedStrip_ColorWheel(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  if (pos < 85U)
  {
    red = (uint8_t)(255U - pos * 3U);
    green = (uint8_t)(pos * 3U);
    blue = 0U;
  }
  else if (pos < 170U)
  {
    pos = (uint8_t)(pos - 85U);
    red = 0U;
    green = (uint8_t)(255U - pos * 3U);
    blue = (uint8_t)(pos * 3U);
  }
  else
  {
    pos = (uint8_t)(pos - 170U);
    red = (uint8_t)(pos * 3U);
    green = 0U;
    blue = (uint8_t)(255U - pos * 3U);
  }

  /* Limit brightness for 20 LEDs. */
  *r = (uint8_t)(red >> 2);
  *g = (uint8_t)(green >> 2);
  *b = (uint8_t)(blue >> 2);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (g_peripherals_initialized == 0U)
  {
    return;
  }

  if (htim->Instance == TIM1)
  {
    HAL_TIMEx_PWMN_Stop_DMA(htim, TIM_CHANNEL_2);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_2, 0U);
    led_strip_busy = 0U;
  }
}








