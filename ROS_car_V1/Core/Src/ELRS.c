#include "ELRS.h"
#include "Mecanum.h"
#include "uart_dma_rx.h"
#include "runtime_clock.h"
#include <string.h>

#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8U
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16U
#define CRSF_MAX_FRAME_SIZE 64U
#define CRSF_RC_PAYLOAD_SIZE 22U
#define CRSF_CRC_POLY 0xD5U

#define ELRS_CHANNEL_MIN 172U
#define ELRS_CHANNEL_MID 992U
#define ELRS_CHANNEL_MAX 1811U
#define ELRS_CHANNEL_DEADBAND 10
#define ELRS_SWITCH_ACTIVE 1500U
#define ELRS_ARM_ON_THRESHOLD 1600U
#define ELRS_ARM_OFF_THRESHOLD 1400U
#define ELRS_ARM_STABLE_FRAMES 3U
#define ELRS_LINK_TIMEOUT_MS 300U
#define ELRS_BYTE_TIMEOUT_MS 10U
#define ELRS_MAX_SPEED_CPS 1000L
#define ELRS_MAX_YAW_CPS 500L

/*
 * Channel mapping for common ELRS/CRSF radio setup:
 *   CH1 Roll  -> vy, left positive
 *   CH2 Pitch -> vx, forward positive
 *   CH4 Yaw   -> wz, counterclockwise positive
 *   CH5       -> arm/unlock switch, high means unlocked
 *   CH8       -> alarm switch, high means alarm active
 *
 * If a stick direction is opposite on the real radio, change only these signs.
 */
#define ELRS_ROLL_SIGN   1L
#define ELRS_PITCH_SIGN  1L
#define ELRS_YAW_SIGN    -1L

typedef enum
{
  ELRS_PARSE_ADDR = 0,
  ELRS_PARSE_LEN,
  ELRS_PARSE_PAYLOAD
} ELRS_ParseState_t;

static UART_HandleTypeDef *elrs_uart = NULL;
static UART_DMA_RX_t elrs_rx;
static uint32_t elrs_received_tick = 0U;
static uint32_t elrs_last_byte_tick = 0U;
static volatile uint8_t elrs_have_frame = 0U;
static uint8_t elrs_frame[CRSF_MAX_FRAME_SIZE];
static uint8_t elrs_frame_len = 0U;
static uint8_t elrs_frame_index = 0U;
static ELRS_ParseState_t elrs_parse_state = ELRS_PARSE_ADDR;
static uint16_t elrs_channels[ELRS_CHANNEL_COUNT] = {0};
static volatile uint32_t elrs_last_frame_tick = 0U;
static uint32_t elrs_frame_sequence;
static ELRS_Control_t elrs_control = {0};
static uint8_t elrs_arm_event = 0U;
static uint8_t elrs_arm_candidate = 0U;
static uint8_t elrs_arm_stable_count = 0U;

static void ELRS_ParseByte(uint8_t data);
static void ELRS_HandleFrame(const uint8_t *frame, uint8_t frame_len);
static uint8_t ELRS_Crc8(const uint8_t *data, uint8_t len);
static void ELRS_UnpackChannels(const uint8_t payload[CRSF_RC_PAYLOAD_SIZE]);
static int32_t ELRS_ChannelToSigned(uint16_t value);
static int32_t ELRS_ScaleSigned(int32_t value, int32_t max_output);
static void ELRS_UpdateControl(void);

void ELRS_Init(UART_HandleTypeDef *huart)
{
  elrs_uart = huart;
  elrs_parse_state = ELRS_PARSE_ADDR;
  elrs_frame_len = 0U;
  elrs_frame_index = 0U;
  elrs_last_frame_tick = 0U;
  elrs_have_frame = 0U;
  elrs_frame_sequence = 0U;
  elrs_control.vx_cps = 0;
  elrs_control.vy_cps = 0;
  elrs_control.wz_cps = 0;
  elrs_control.link_active = 0U;
  elrs_control.armed = 0U;
  elrs_control.alarm_active = 0U;
  elrs_arm_event = 0U;
  elrs_arm_candidate = 0U;
  elrs_arm_stable_count = 0U;

  for (uint8_t i = 0U; i < ELRS_CHANNEL_COUNT; i++)
  {
    elrs_channels[i] = ELRS_CHANNEL_MID;
  }

  if (elrs_uart != NULL)
  {
    (void)UART_DMA_RX_Init(&elrs_rx, elrs_uart);
  }
}

void ELRS_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position)
{
  (void)position;
  if ((elrs_uart == NULL) || (huart != elrs_uart))
  {
    return;
  }

  UART_DMA_RX_Event(&elrs_rx);
}

void ELRS_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((elrs_uart != NULL) && (huart == elrs_uart)) UART_DMA_RX_Error(&elrs_rx);
}

void ELRS_RxTick(void) { if (elrs_uart != NULL) UART_DMA_RX_Event(&elrs_rx); }

static void ELRS_Invalidate(void)
{
  memset(&elrs_control, 0, sizeof(elrs_control));
  elrs_arm_event = 0U;
  elrs_arm_candidate = 0U;
  elrs_arm_stable_count = 0U;
  elrs_have_frame = 0U;
  elrs_parse_state = ELRS_PARSE_ADDR;
  elrs_frame_index = 0U;
}

void ELRS_Poll(void)
{
  if (elrs_uart == NULL) return;
  if (UART_DMA_RX_Service(&elrs_rx) != 0U) ELRS_Invalidate();
  /* Expire the old arm state before a new frame can refresh its timestamp. */
  if ((elrs_have_frame != 0U) &&
      ((uint32_t)(HAL_GetTick() - elrs_last_frame_tick) > ELRS_LINK_TIMEOUT_MS))
    ELRS_Invalidate();
  uint32_t started_at = RuntimeClock_Cycles();
  for (uint16_t i = 0U; i < UART_DMA_RX_PARSE_BYTES; ++i)
  {
    uint8_t byte;
    int result = UART_DMA_RX_Read(&elrs_rx, &byte, &elrs_received_tick);
    if (result <= 0)
    {
      if (result < 0) ELRS_Invalidate();
      break;
    }
    if ((elrs_parse_state != ELRS_PARSE_ADDR) &&
        ((uint32_t)(elrs_received_tick - elrs_last_byte_tick) > ELRS_BYTE_TIMEOUT_MS))
    {
      elrs_parse_state = ELRS_PARSE_ADDR;
      elrs_frame_index = 0U;
    }
    elrs_last_byte_tick = elrs_received_tick;
    ELRS_ParseByte(byte);
    if (RuntimeClock_BudgetExpired(started_at, UART_DMA_RX_PARSE_US) != 0U) break;
  }
  if ((elrs_have_frame != 0U) &&
      ((uint32_t)(HAL_GetTick() - elrs_last_frame_tick) > ELRS_LINK_TIMEOUT_MS))
    ELRS_Invalidate();
}

void ELRS_Process(uint32_t now_ms)
{
  if (ELRS_IsLinkActive(now_ms) == 0U)
  {
    Mecanum_Stop();

    elrs_control.vx_cps = 0;
    elrs_control.vy_cps = 0;
    elrs_control.wz_cps = 0;
    elrs_control.link_active = 0U;
    elrs_control.armed = 0U;
    elrs_control.alarm_active = 0U;
    return;
  }

  elrs_control.link_active = 1U;

  if (elrs_control.armed == 0U)
  {
    Mecanum_Stop();
    return;
  }

  Mecanum_SetVelocity(elrs_control.vx_cps, elrs_control.vy_cps, elrs_control.wz_cps);
}

void ELRS_ProcessNoDrive(uint32_t now_ms)
{
  if (ELRS_IsLinkActive(now_ms) == 0U)
  {
    if (elrs_control.link_active != 0U)
    {
      Mecanum_Stop();
    }

    elrs_control.vx_cps = 0;
    elrs_control.vy_cps = 0;
    elrs_control.wz_cps = 0;
    elrs_control.link_active = 0U;
    elrs_control.armed = 0U;
    elrs_control.alarm_active = 0U;
    return;
  }

  elrs_control.link_active = 1U;

  if (elrs_control.armed == 0U)
  {
    Mecanum_Stop();
  }
}

uint8_t ELRS_GetControl(ELRS_Control_t *control)
{
  if (control == NULL)
  {
    return 0U;
  }

  *control = elrs_control;
  return elrs_control.link_active;
}

uint8_t ELRS_GetSnapshot(ELRS_Snapshot_t *snapshot)
{
  if (snapshot == NULL || ELRS_IsLinkActive(HAL_GetTick()) == 0U) return 0U;
  snapshot->control = elrs_control;
  memcpy(snapshot->channels, elrs_channels, sizeof(elrs_channels));
  snapshot->received_ms = elrs_last_frame_tick;
  snapshot->sequence = elrs_frame_sequence;
  return ELRS_IsLinkActive(HAL_GetTick());
}

uint16_t ELRS_GetChannel(uint8_t channel_index)
{
  if (channel_index >= ELRS_CHANNEL_COUNT)
  {
    return ELRS_CHANNEL_MID;
  }

  return elrs_channels[channel_index];
}

uint8_t ELRS_IsLinkActive(uint32_t now_ms)
{
  if ((elrs_have_frame == 0U) || (elrs_rx.restart != 0U))
  {
    return 0U;
  }

  (void)now_ms;
  return ((uint32_t)(HAL_GetTick() - elrs_last_frame_tick) <= ELRS_LINK_TIMEOUT_MS) ? 1U : 0U;
}

uint8_t ELRS_IsArmed(void)
{
  return ELRS_IsLinkActive(HAL_GetTick()) != 0U ? elrs_control.armed : 0U;
}

uint8_t ELRS_IsAlarmActive(void)
{
  return elrs_control.alarm_active;
}

uint8_t ELRS_ConsumeArmEvent(void)
{
  uint8_t event = elrs_arm_event;
  elrs_arm_event = 0U;
  return event;
}

static void ELRS_ParseByte(uint8_t data)
{
  switch (elrs_parse_state)
  {
    case ELRS_PARSE_ADDR:
      if (data == CRSF_ADDRESS_FLIGHT_CONTROLLER)
      {
        elrs_parse_state = ELRS_PARSE_LEN;
      }
      break;

    case ELRS_PARSE_LEN:
      if ((data < 2U) || (data > (CRSF_MAX_FRAME_SIZE - 2U)))
      {
        elrs_parse_state = ELRS_PARSE_ADDR;
        break;
      }
      elrs_frame_len = data;
      elrs_frame_index = 0U;
      elrs_parse_state = ELRS_PARSE_PAYLOAD;
      break;

    case ELRS_PARSE_PAYLOAD:
      elrs_frame[elrs_frame_index++] = data;
      if (elrs_frame_index >= elrs_frame_len)
      {
        ELRS_HandleFrame(elrs_frame, elrs_frame_len);
        elrs_parse_state = ELRS_PARSE_ADDR;
      }
      break;

    default:
      elrs_parse_state = ELRS_PARSE_ADDR;
      break;
  }
}

static void ELRS_HandleFrame(const uint8_t *frame, uint8_t frame_len)
{
  uint8_t type;
  uint8_t payload_len;
  uint8_t crc;

  if ((frame == NULL) || (frame_len < 2U))
  {
    return;
  }

  crc = ELRS_Crc8(frame, (uint8_t)(frame_len - 1U));
  if (crc != frame[frame_len - 1U])
  {
    return;
  }

  type = frame[0];
  payload_len = (uint8_t)(frame_len - 2U);

  if ((type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED) && (payload_len == CRSF_RC_PAYLOAD_SIZE))
  {
    if ((uint32_t)(HAL_GetTick() - elrs_received_tick) > ELRS_LINK_TIMEOUT_MS) return;
    if (elrs_rx.restart != 0U) return;
    ELRS_UnpackChannels(&frame[1]);
    elrs_last_frame_tick = elrs_received_tick;
    elrs_have_frame = 1U;
    ++elrs_frame_sequence;
    elrs_control.link_active = 1U;
    ELRS_UpdateControl();
  }
}

void ELRS_GetStatistics(ELRS_Statistics_t *statistics)
{
  if (statistics == NULL) return;
  uint32_t mask = __get_PRIMASK();
  __disable_irq();
  statistics->uart_errors = elrs_rx.errors;
  statistics->rx_overruns = elrs_rx.overruns;
  statistics->rx_restart_errors = elrs_rx.restart_errors;
  __set_PRIMASK(mask);
}

static uint8_t ELRS_Crc8(const uint8_t *data, uint8_t len)
{
  uint8_t crc = 0U;

  for (uint8_t i = 0U; i < len; i++)
  {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x80U) != 0U)
      {
        crc = (uint8_t)((crc << 1U) ^ CRSF_CRC_POLY);
      }
      else
      {
        crc <<= 1U;
      }
    }
  }

  return crc;
}

static void ELRS_UnpackChannels(const uint8_t payload[CRSF_RC_PAYLOAD_SIZE])
{
  uint32_t bit_buffer = 0U;
  uint8_t bits_in_buffer = 0U;
  uint8_t byte_index = 0U;

  for (uint8_t ch = 0U; ch < ELRS_CHANNEL_COUNT; ch++)
  {
    while (bits_in_buffer < 11U)
    {
      bit_buffer |= ((uint32_t)payload[byte_index++]) << bits_in_buffer;
      bits_in_buffer = (uint8_t)(bits_in_buffer + 8U);
    }

    elrs_channels[ch] = (uint16_t)(bit_buffer & 0x07FFU);
    bit_buffer >>= 11U;
    bits_in_buffer = (uint8_t)(bits_in_buffer - 11U);
  }
}

static int32_t ELRS_ChannelToSigned(uint16_t value)
{
  int32_t signed_value = (int32_t)value - (int32_t)ELRS_CHANNEL_MID;

  if ((signed_value > -ELRS_CHANNEL_DEADBAND) && (signed_value < ELRS_CHANNEL_DEADBAND))
  {
    return 0;
  }

  if (signed_value > ((int32_t)ELRS_CHANNEL_MAX - (int32_t)ELRS_CHANNEL_MID))
  {
    signed_value = (int32_t)ELRS_CHANNEL_MAX - (int32_t)ELRS_CHANNEL_MID;
  }
  else if (signed_value < ((int32_t)ELRS_CHANNEL_MIN - (int32_t)ELRS_CHANNEL_MID))
  {
    signed_value = (int32_t)ELRS_CHANNEL_MIN - (int32_t)ELRS_CHANNEL_MID;
  }

  return signed_value;
}

static int32_t ELRS_ScaleSigned(int32_t value, int32_t max_output)
{
  int32_t denom;

  if (value >= 0)
  {
    denom = (int32_t)ELRS_CHANNEL_MAX - (int32_t)ELRS_CHANNEL_MID;
  }
  else
  {
    denom = (int32_t)ELRS_CHANNEL_MID - (int32_t)ELRS_CHANNEL_MIN;
  }

  if (denom <= 0)
  {
    return 0;
  }

  return (value * max_output) / denom;
}

static void ELRS_UpdateControl(void)
{
  int32_t roll = ELRS_ChannelToSigned(elrs_channels[0]);
  int32_t pitch = ELRS_ChannelToSigned(elrs_channels[1]);
  int32_t yaw = ELRS_ChannelToSigned(elrs_channels[3]);
  uint8_t armed_sample = elrs_control.armed;

  /*
   * CH5 arm switch filter:
   * - use hysteresis to avoid 1500-near threshold chatter;
   * - require several consecutive frames before accepting state change.
   */
  if (elrs_channels[4] >= ELRS_ARM_ON_THRESHOLD)
  {
    armed_sample = 1U;
  }
  else if (elrs_channels[4] <= ELRS_ARM_OFF_THRESHOLD)
  {
    armed_sample = 0U;
  }

  if (armed_sample == elrs_arm_candidate)
  {
    if (elrs_arm_stable_count < ELRS_ARM_STABLE_FRAMES)
    {
      elrs_arm_stable_count++;
    }
  }
  else
  {
    elrs_arm_candidate = armed_sample;
    elrs_arm_stable_count = 1U;
  }

  if ((elrs_arm_stable_count >= ELRS_ARM_STABLE_FRAMES) &&
      (elrs_arm_candidate != elrs_control.armed))
  {
    if ((elrs_arm_candidate != 0U) && (elrs_control.armed == 0U))
    {
      elrs_arm_event = 1U;
    }
    elrs_control.armed = elrs_arm_candidate;
  }

  elrs_control.alarm_active = (elrs_channels[7] > ELRS_SWITCH_ACTIVE) ? 1U : 0U;

  if (elrs_control.armed == 0U)
  {
    elrs_control.vx_cps = 0;
    elrs_control.vy_cps = 0;
    elrs_control.wz_cps = 0;
    return;
  }

  elrs_control.vx_cps = ELRS_PITCH_SIGN * ELRS_ScaleSigned(pitch, ELRS_MAX_SPEED_CPS);
  elrs_control.vy_cps = ELRS_ROLL_SIGN * ELRS_ScaleSigned(roll, ELRS_MAX_SPEED_CPS);
  elrs_control.wz_cps = ELRS_YAW_SIGN * ELRS_ScaleSigned(yaw, ELRS_MAX_YAW_CPS);
}

