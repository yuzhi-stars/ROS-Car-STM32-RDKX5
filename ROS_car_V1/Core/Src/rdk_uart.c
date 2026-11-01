#include "rdk_uart.h"
#include "Motor.h"
#include "imu_attitude.h"
#include "uart_dma_rx.h"
#include "runtime_clock.h"
#include <float.h>
#include <math.h>
#include <string.h>

/* Wire format: AA 55 TYPE float32-LE payload FLAGS CRC_LO CRC_HI.
 * Commands (03) have no FLAGS. CRC covers TYPE+payload only. */
#define RDK_TWO_PI 6.283185307f
#define RDK_GYRO_RAD_PER_LSB (0.00875f * 0.01745329252f)
#define RDK_ACCEL_MPS2_PER_LSB (0.000061f * 9.80665f)
#define RDK_FEEDBACK_MAX_AGE_MS 200U
#define RDK_TX_TIMEOUT_MS 40U  /* 68 bytes need about 5.9 ms at 115200 baud. */

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "RDK requires IEEE 754 binary32");

static UART_HandleTypeDef *s_uart;
static UART_DMA_RX_t s_rx;
static uint8_t s_rx_frame[RDK_UART_IMU_FRAME_SIZE];
static uint8_t s_rx_size;
static uint32_t s_rx_last_byte_ms;
static uint8_t s_tx_frame[RDK_UART_WHEEL_FRAME_SIZE + RDK_UART_IMU_FRAME_SIZE];
static volatile uint8_t s_tx_busy;
static volatile uint8_t s_tx_recover;
static uint8_t s_tx_retry_wait;
static uint32_t s_tx_retry_ms;
static uint32_t s_last_tx_ms;
static uint32_t s_last_wheel_sample_ms;
static volatile uint8_t s_command_ready;
static volatile uint8_t s_control_enabled;
static volatile uint8_t s_require_zero;
static volatile uint8_t s_rx_flush_requested;
static volatile uint32_t s_last_command_ms;
static volatile int32_t s_target_cps[4];
static RDK_UART_Statistics_t s_statistics;

/* IMU producer and transmitter both run in main, never inside UART ISRs. */
static float s_imu[10];
static float s_gyro_bias_raw[3];
static uint8_t s_imu_pending_flags;
static uint32_t s_imu_sample_ms[3];
static IMU_Attitude_t s_attitude;
static uint32_t s_last_gyro_ms;
static uint8_t s_have_gyro_time;
static uint8_t s_bias_valid;
static uint8_t s_orientation_valid;
static RDK_UART_ImuDiagnostics_t s_imu_diagnostics;

static uint32_t RDK_Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

static void RDK_Unlock(uint32_t mask)
{
    __set_PRIMASK(mask);
}

uint16_t RDK_UART_Crc16(const uint8_t *data, size_t size)
{
    uint16_t crc = 0xFFFFU;
    if ((data == NULL) && (size != 0U)) return 0U;
    for (size_t i = 0U; i < size; ++i)
    {
        crc ^= (uint16_t)((uint16_t)data[i] << 8U);
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) != 0U ?
                (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
    }
    return crc;
}

static void RDK_PutFloat(uint8_t *bytes, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (uint8_t i = 0U; i < 4U; ++i)
        bytes[i] = (uint8_t)(bits >> (8U * i));
}

static float RDK_GetFloat(const uint8_t *bytes)
{
    uint32_t bits = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
                    ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void RDK_FinishFrame(uint8_t *frame, uint16_t size)
{
    uint16_t crc = RDK_UART_Crc16(&frame[2], size - 4U);
    frame[size - 2U] = (uint8_t)crc;
    frame[size - 1U] = (uint8_t)(crc >> 8U);
}

void RDK_UART_PackWheelFeedback(const float velocity[4], uint8_t valid_flags,
                                uint8_t frame[RDK_UART_WHEEL_FRAME_SIZE])
{
    memset(frame, 0, RDK_UART_WHEEL_FRAME_SIZE);
    frame[0] = 0xAAU;
    frame[1] = 0x55U;
    frame[2] = 0x90U;
    valid_flags &= RDK_UART_ALL_WHEELS_VALID;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        uint8_t bit = (uint8_t)(1U << i);
        if ((velocity != NULL) && ((valid_flags & bit) != 0U) && isfinite(velocity[i]))
            RDK_PutFloat(&frame[3U + 4U * i], velocity[i]);
        else
            valid_flags &= (uint8_t)~bit;
    }
    frame[19] = valid_flags;
    RDK_FinishFrame(frame, RDK_UART_WHEEL_FRAME_SIZE);
}

static uint8_t RDK_NormalizeOrientation(const float input[4], float output[4])
{
    float sum = 0.0f;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        /* Bound before squaring: even a finite malicious input cannot overflow. */
        if (!isfinite(input[i]) || fabsf(input[i]) > 1.01f) return 0U;
        sum += input[i] * input[i];
    }
    float norm = sqrtf(sum);
    if (fabsf(norm - 1.0f) > 0.01f) return 0U;
    float inverse = 1.0f / norm;
    for (uint8_t i = 0U; i < 4U; ++i) output[i] = input[i] * inverse;
    return 1U;
}

static void RDK_PackImu(const float values[10], uint8_t valid_flags,
                        uint8_t frame[RDK_UART_IMU_FRAME_SIZE], uint8_t trusted)
{
    static const uint8_t starts[3] = {0U, 4U, 7U};
    static const uint8_t counts[3] = {4U, 3U, 3U};
    memset(frame, 0, RDK_UART_IMU_FRAME_SIZE);
    frame[0] = 0xAAU;
    frame[1] = 0x55U;
    frame[2] = 0x91U;
    valid_flags &= 0x07U;
    for (uint8_t group = 0U; group < 3U; ++group)
    {
        uint8_t bit = (uint8_t)(1U << group);
        uint8_t valid = (uint8_t)((values != NULL) && ((valid_flags & bit) != 0U));
        float normalized[4];
        if (valid != 0U)
        {
            for (uint8_t i = starts[group]; i < starts[group] + counts[group]; ++i)
                if (!isfinite(values[i])) valid = 0U;
            if ((group == 0U) && (valid != 0U) && (trusted == 0U))
            {
                valid = RDK_NormalizeOrientation(values, normalized);
            }
        }
        if (valid != 0U)
        {
            for (uint8_t i = starts[group]; i < starts[group] + counts[group]; ++i)
                RDK_PutFloat(&frame[3U + 4U * i],
                    (group == 0U && trusted == 0U) ? normalized[i] : values[i]);
        }
        else valid_flags &= (uint8_t)~bit;
    }
    frame[43] = valid_flags;
    RDK_FinishFrame(frame, RDK_UART_IMU_FRAME_SIZE);
}

void RDK_UART_PackImuFeedback(const float values[10], uint8_t valid_flags,
                              uint8_t frame[RDK_UART_IMU_FRAME_SIZE])
{
    RDK_PackImu(values, valid_flags, frame, 0U);
}

/* Call with interrupts disabled, or from Init before the timer is active. */
static void RDK_LockMotion(void)
{
    s_command_ready = 0U;
    for (uint8_t i = 0U; i < 4U; ++i) s_target_cps[i] = 0;
}

static void RDK_CheckTimeout(uint32_t now_ms)
{
    if ((s_command_ready != 0U) &&
        ((uint32_t)(now_ms - s_last_command_ms) >= RDK_UART_COMMAND_TIMEOUT_MS))
    {
        RDK_LockMotion();
        ++s_statistics.command_timeouts;
        if (s_control_enabled != 0U) Motor_ClosedLoopEmergencyStop();
    }
}

static void RDK_AcceptCommand(uint32_t now_ms)
{
    uint32_t current_ms = HAL_GetTick();
    if ((uint32_t)(current_ms - now_ms) >= RDK_UART_COMMAND_TIMEOUT_MS)
    {
        ++s_statistics.stale_commands;
        return;
    }
    float speed[4];
    int32_t targets[4];
    uint8_t all_zero = 1U;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        speed[i] = RDK_GetFloat(&s_rx_frame[3U + 4U * i]);
        if (!isfinite(speed[i]) || fabsf(speed[i]) > RDK_UART_MAX_SPEED_RAD_S)
        {
            ++s_statistics.invalid_commands;
            return;
        }
        if (speed[i] != 0.0f) all_zero = 0U;
        targets[i] = (int32_t)lroundf(speed[i] * RDK_UART_COUNTS_PER_WHEEL_REV / RDK_TWO_PI);
    }

    uint32_t mask = RDK_Lock();
    if (s_control_enabled == 0U || s_rx_flush_requested != 0U)
    {
        ++s_statistics.commands_while_disabled;
        RDK_Unlock(mask);
        return;
    }
    /* Expire the previous target before accepting the next received frame. */
    RDK_CheckTimeout(HAL_GetTick());
    if (s_rx.restart != 0U)
    {
        RDK_Unlock(mask);
        return;
    }
    if ((s_require_zero != 0U) && (s_command_ready == 0U) && (all_zero == 0U))
    {
        ++s_statistics.commands_before_zero;
        RDK_Unlock(mask);
        return;
    }
    for (uint8_t i = 0U; i < 4U; ++i) s_target_cps[i] = targets[i];
    s_last_command_ms = now_ms;
    s_command_ready = 1U;
    ++s_statistics.command_frames;
    RDK_Unlock(mask);
}

static void RDK_DiscardFront(uint8_t count)
{
    s_rx_size = (uint8_t)(s_rx_size - count);
    memmove(s_rx_frame, &s_rx_frame[count], s_rx_size);
}

/* Bounded sliding parser: accepts arbitrary fragments, noise and concatenation.
 * Known feedback frames are CRC-checked and consumed as echoes, never commands. */
static void RDK_FeedByte(uint8_t byte, uint32_t now_ms)
{
    if ((uint32_t)(now_ms - s_rx_last_byte_ms) >= RDK_UART_COMMAND_TIMEOUT_MS)
        s_rx_size = 0U;
    s_rx_last_byte_ms = now_ms;
    s_rx_frame[s_rx_size++] = byte;
    while (s_rx_size != 0U)
    {
        if (s_rx_frame[0] != 0xAAU) { RDK_DiscardFront(1U); continue; }
        if (s_rx_size < 2U) return;
        if (s_rx_frame[1] != 0x55U) { RDK_DiscardFront(1U); continue; }
        if (s_rx_size < 3U) return;
        uint8_t size;
        switch (s_rx_frame[2])
        {
            case 0x03U: size = RDK_UART_SPEED_FRAME_SIZE; break;
            case 0x90U: size = RDK_UART_WHEEL_FRAME_SIZE; break;
            case 0x91U: size = RDK_UART_IMU_FRAME_SIZE; break;
            default: RDK_DiscardFront(1U); continue;
        }
        if (s_rx_size < size) return;
        uint16_t received_crc = (uint16_t)((uint16_t)s_rx_frame[size - 2U] |
                                 ((uint16_t)s_rx_frame[size - 1U] << 8U));
        if (received_crc != RDK_UART_Crc16(&s_rx_frame[2], size - 4U))
        {
            ++s_statistics.crc_errors;
            RDK_DiscardFront(1U);
            continue;
        }
        if (s_rx_frame[2] == 0x03U) RDK_AcceptCommand(now_ms);
        RDK_DiscardFront(size);
    }
}

HAL_StatusTypeDef RDK_UART_Init(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->hdmarx == NULL) || (huart->hdmatx == NULL) ||
        (s_uart != NULL)) return HAL_ERROR;
    s_uart = huart;
    s_rx_size = 0U;
    s_tx_busy = 0U;
    s_control_enabled = (uint8_t)(RDK_UART_CONTROL_ENABLE != 0U);
    s_require_zero = 1U;
    s_rx_flush_requested = 0U;
    s_last_tx_ms = HAL_GetTick();
    s_rx_last_byte_ms = s_last_tx_ms;
    s_last_wheel_sample_ms = Motor_GetEncoderSampleTick();
    memset(&s_statistics, 0, sizeof(s_statistics));
    memset(s_imu, 0, sizeof(s_imu));
    memset(s_gyro_bias_raw, 0, sizeof(s_gyro_bias_raw));
    memset(&s_imu_diagnostics, 0, sizeof(s_imu_diagnostics));
    s_imu_pending_flags = 0U;
    IMU_Attitude_Init(&s_attitude);
    s_have_gyro_time = 0U;
    s_bias_valid = 0U;
    s_orientation_valid = 0U;
    uint32_t mask = RDK_Lock();
    RDK_LockMotion();
    if (s_control_enabled != 0U) Motor_ClosedLoopEmergencyStop();
    RDK_Unlock(mask);
    return UART_DMA_RX_Init(&s_rx, s_uart);
}

uint8_t RDK_UART_ControlTick(uint32_t now_ms)
{
    uint32_t mask = RDK_Lock();
    if (s_control_enabled == 0U) { RDK_Unlock(mask); return 0U; }
    RDK_CheckTimeout(now_ms);
    uint8_t ready = (uint8_t)((s_uart != NULL) && (s_command_ready != 0U));
    if (ready == 0U) Motor_ClosedLoopEmergencyStop();
    else
        for (uint8_t i = 0U; i < 4U; ++i)
            Motor_ClosedLoopSetTarget((Motor_ID_t)i, s_target_cps[i]);
    RDK_Unlock(mask);
    return ready;
}

static void RDK_SetControlGrant(uint8_t enabled, uint8_t require_zero)
{
    enabled = (uint8_t)(enabled != 0U && RDK_UART_CONTROL_ENABLE != 0U);
    uint32_t mask = RDK_Lock();
    if (s_control_enabled != enabled || s_require_zero != require_zero)
    {
        s_control_enabled = enabled;
        s_require_zero = require_zero;
        RDK_LockMotion();
        /* Parser state is MAIN-owned: a SysTick revoke must not mutate its
         * partial-frame size while a CRC/discard is in progress. */
        s_rx_flush_requested = 1U;
    }
    RDK_Unlock(mask);
}

void RDK_UART_SetControlEnabled(uint8_t enabled)
{
    RDK_SetControlGrant(enabled, 1U);
}

void RDK_UART_GrantFreshControl(void)
{
    RDK_SetControlGrant(1U, 0U);
}

uint8_t RDK_UART_IsControlEnabled(void) { return s_control_enabled; }

void RDK_UART_WatchdogTick(uint32_t now_ms)
{
    if ((s_uart == NULL) || (s_control_enabled == 0U)) return;
    uint32_t mask = RDK_Lock();
    RDK_CheckTimeout(now_ms);
    RDK_Unlock(mask);
}

void RDK_UART_PollRx(void)
{
    if (s_uart == NULL) return;
    uint32_t mask = RDK_Lock();
    if (s_rx_flush_requested != 0U)
    {
        UART_DMA_RX_Discard(&s_rx);
        s_rx_size = 0U;
        s_rx_flush_requested = 0U;
    }
    RDK_Unlock(mask);
    if (UART_DMA_RX_Service(&s_rx) != 0U)
    {
        uint32_t mask = RDK_Lock();
        s_rx_size = 0U;
        RDK_LockMotion();
        RDK_Unlock(mask);
    }
    uint32_t started_at = RuntimeClock_Cycles();
    for (uint16_t i = 0U; i < UART_DMA_RX_PARSE_BYTES; ++i)
    {
        uint8_t byte;
        uint32_t received_ms;
        int result = UART_DMA_RX_Read(&s_rx, &byte, &received_ms);
        if (result <= 0)
        {
            if (result < 0)
            {
                uint32_t mask = RDK_Lock();
                s_rx_size = 0U;
                RDK_LockMotion();
                RDK_Unlock(mask);
            }
            break;
        }
        RDK_FeedByte(byte, received_ms);
        if (RuntimeClock_BudgetExpired(started_at, UART_DMA_RX_PARSE_US) != 0U) break;
    }
}

static uint8_t RDK_TxReady(uint32_t now_ms)
{
    uint32_t mask = RDK_Lock();
    if ((s_tx_recover == 0U) &&
        (((s_tx_busy != 0U) && ((uint32_t)(now_ms - s_last_tx_ms) >= RDK_TX_TIMEOUT_MS)) ||
         ((s_tx_busy == 0U) && (s_uart->gState != HAL_UART_STATE_READY))))
    {
        s_tx_recover = 1U;
        ++s_statistics.tx_errors;
    }
    uint8_t recover = s_tx_recover;
    uint8_t busy = s_tx_busy;
    RDK_Unlock(mask);
    if (recover == 0U) return (uint8_t)(busy == 0U);
    if ((s_tx_retry_wait != 0U) && ((uint32_t)(now_ms - s_tx_retry_ms) < 10U)) return 0U;
    s_tx_retry_wait = 1U;
    s_tx_retry_ms = now_ms;
    /* Main-only: HAL abort can poll. Keep the source buffer immutable until
     * both HAL and the physical DMA stream have relinquished it. */
    HAL_StatusTypeDef status = HAL_UART_AbortTransmit(s_uart);
    if (status != HAL_OK) return 0U;
    if (UART_DMA_ResetStopped(s_uart->hdmatx) != HAL_OK) return 0U;
    mask = RDK_Lock();
    s_tx_busy = s_tx_recover = s_tx_retry_wait = 0U;
    RDK_Unlock(mask);
    return 1U;
}

void RDK_UART_Process(uint32_t now_ms)
{
    if (s_uart == NULL) return;
    RDK_UART_PollRx();
    now_ms = HAL_GetTick();
    if ((RDK_TxReady(now_ms) == 0U) ||
        ((uint32_t)(now_ms - s_last_tx_ms) < RDK_UART_FEEDBACK_PERIOD_MS)) return;

    int32_t cps[4];
    float velocity[4];
    uint32_t mask = RDK_Lock();
    Motor_GetAllEncoderSpeeds(cps);
    uint32_t sample_ms = Motor_GetEncoderSampleTick();
    RDK_Unlock(mask);
    /* Take the age reference AFTER the sample snapshot, not before a timer
     * preemption which might publish a newer sample timestamp. */
    now_ms = HAL_GetTick();
    uint8_t wheel_flags = 0U;
    if ((sample_ms != s_last_wheel_sample_ms) &&
        ((uint32_t)(now_ms - sample_ms) < RDK_FEEDBACK_MAX_AGE_MS))
        wheel_flags = RDK_UART_ALL_WHEELS_VALID;
    for (uint8_t i = 0U; i < 4U; ++i)
        velocity[i] = (float)cps[i] * RDK_TWO_PI / RDK_UART_COUNTS_PER_WHEEL_REV;

    uint8_t imu_flags = s_imu_pending_flags;
    for (uint8_t group = 0U; group < 3U; ++group)
        if ((uint32_t)(now_ms - s_imu_sample_ms[group]) >=
            (group == 0U ? 50U : RDK_FEEDBACK_MAX_AGE_MS))
            imu_flags &= (uint8_t)~(1U << group);
    RDK_UART_PackWheelFeedback(velocity, wheel_flags, s_tx_frame);
    RDK_PackImu(s_imu, imu_flags, &s_tx_frame[RDK_UART_WHEEL_FRAME_SIZE], 1U);
    mask = RDK_Lock();
    /* Serialize the start with a DMA error arriving during packet packing. */
    if (s_tx_recover != 0U) { RDK_Unlock(mask); return; }
    s_tx_busy = 1U;
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(s_uart, s_tx_frame, sizeof(s_tx_frame));
    s_last_tx_ms = now_ms;
    RDK_Unlock(mask);
    if (status == HAL_OK)
    {
        s_last_wheel_sample_ms = sample_ms;
        s_imu_pending_flags = 0U;  /* Never mark the same sample valid twice. */
    }
    else
    {
        s_tx_busy = 0U;
        mask = RDK_Lock();
        ++s_statistics.tx_errors;
        RDK_Unlock(mask);
    }
}

void RDK_UART_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position)
{
    (void)position;
    if ((s_uart == NULL) || (huart != s_uart)) return;
    UART_DMA_RX_Event(&s_rx);
}

void RDK_UART_RxTick(void) { if (s_uart != NULL) UART_DMA_RX_Event(&s_rx); }

void RDK_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((s_uart == NULL) || (huart != s_uart)) return;
    s_tx_busy = 0U;
    s_statistics.tx_frames += 2U;
}

void RDK_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((s_uart == NULL) || (huart != s_uart)) return;
    uint32_t mask = RDK_Lock();
    ++s_statistics.uart_errors;
    RDK_LockMotion();
    if (s_control_enabled != 0U) Motor_ClosedLoopEmergencyStop();
    UART_DMA_RX_Error(&s_rx);
    /* A DMA error on RX also makes HAL end TX without stopping its stream.
     * Defer abort/release to main; an ISR must not release the source buffer. */
    if ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0U)
    {
        if (s_tx_recover == 0U) ++s_statistics.tx_errors;
        s_tx_recover = 1U;
    }
    RDK_Unlock(mask);
}

void RDK_UART_SetGyroBiasRaw(float x, float y, float z)
{
    if (!isfinite(x) || !isfinite(y) || !isfinite(z))
    {
        s_bias_valid = 0U;
        IMU_Attitude_Init(&s_attitude);
        s_have_gyro_time = 0U;
        RDK_UART_UpdateOrientation(NULL, 0U);
        return;
    }
    if ((s_bias_valid == 0U) || (x != s_gyro_bias_raw[0]) ||
        (y != s_gyro_bias_raw[1]) || (z != s_gyro_bias_raw[2]))
    {
        IMU_Attitude_Init(&s_attitude);
        s_have_gyro_time = 0U;
        RDK_UART_UpdateOrientation(NULL, 0U);
    }
    s_gyro_bias_raw[0] = x;
    s_gyro_bias_raw[1] = y;
    s_gyro_bias_raw[2] = z;
    s_bias_valid = 1U;
}

void RDK_UART_UpdateImuRaw(const IMU_AxisRaw_t *gyro,
                           const IMU_AxisRaw_t *accel, uint8_t fresh_flags)
{
    uint32_t now_ms = HAL_GetTick();
    s_imu_diagnostics.last_fresh_flags = fresh_flags;
    /* Retain untransmitted NEW groups across separate DRDY phases. A gyro
     * sample must not erase an accel sampled since the previous UART frame.
     * flags=0 explicitly invalidates a failed/unavailable read as before. */
    if (fresh_flags == 0U)
    {
        s_imu_pending_flags = 0U;
        s_orientation_valid = 0U;
    }
    if ((gyro != NULL) && ((fresh_flags & IMU_MOTION_GYRO_FRESH) != 0U))
    {
        s_imu_pending_flags &= (uint8_t)~RDK_UART_ORIENTATION_VALID;
        s_orientation_valid = 0U;
        if (s_imu_diagnostics.gyro_samples != 0U)
        {
            uint32_t gap = (uint32_t)(now_ms - s_imu_sample_ms[1]);
            if (gap > s_imu_diagnostics.max_gyro_gap_ms) s_imu_diagnostics.max_gyro_gap_ms = gap;
            if (gap > 50U && s_have_gyro_time != 0U) ++s_imu_diagnostics.gyro_gap_resets;
        }
        ++s_imu_diagnostics.gyro_samples;
        s_imu[4] = ((float)gyro->x - s_gyro_bias_raw[0]) * RDK_GYRO_RAD_PER_LSB;
        s_imu[5] = ((float)gyro->y - s_gyro_bias_raw[1]) * RDK_GYRO_RAD_PER_LSB;
        s_imu[6] = ((float)gyro->z - s_gyro_bias_raw[2]) * RDK_GYRO_RAD_PER_LSB;
        s_imu_pending_flags |= RDK_UART_GYRO_VALID;
        s_imu_sample_ms[1] = HAL_GetTick();
    }
    if ((accel != NULL) && ((fresh_flags & IMU_MOTION_ACCEL_FRESH) != 0U))
    {
        ++s_imu_diagnostics.accel_samples;
        s_imu[7] = (float)accel->x * RDK_ACCEL_MPS2_PER_LSB;
        s_imu[8] = (float)accel->y * RDK_ACCEL_MPS2_PER_LSB;
        s_imu[9] = (float)accel->z * RDK_ACCEL_MPS2_PER_LSB;
        s_imu_pending_flags |= RDK_UART_ACCEL_VALID;
        s_imu_sample_ms[2] = HAL_GetTick();
    }

    if ((gyro == NULL) || ((fresh_flags & IMU_MOTION_GYRO_FRESH) == 0U))
    {
        /* No fresh gyro means no new orientation. Do not integrate old data. */
        if ((s_have_gyro_time != 0U) && ((uint32_t)(now_ms - s_last_gyro_ms) > 50U))
        {
            ++s_imu_diagnostics.gyro_gap_resets;
            IMU_Attitude_Init(&s_attitude);
            s_have_gyro_time = 0U;
            s_imu_pending_flags &= (uint8_t)~RDK_UART_ORIENTATION_VALID;
            s_orientation_valid = 0U;
        }
        return;
    }
    if (s_bias_valid == 0U) return;  /* Startup calibration has not succeeded. */
    uint32_t elapsed_ms = s_have_gyro_time != 0U ? (uint32_t)(now_ms - s_last_gyro_ms) : 10U;
    if (elapsed_ms == 0U) return;
    s_last_gyro_ms = now_ms;
    s_have_gyro_time = 1U;
    const float *acceleration = (accel != NULL && (fresh_flags & IMU_MOTION_ACCEL_FRESH) != 0U) ?
                                &s_imu[7] : NULL;
    uint16_t previous_alignment = s_attitude.alignment_samples;
    uint8_t valid = IMU_Attitude_Update(&s_attitude, &s_imu[4], acceleration,
                                       (float)elapsed_ms * 0.001f);
    if (previous_alignment != 0U && s_attitude.alignment_samples < previous_alignment)
        ++s_imu_diagnostics.alignment_resets;
    /* Brief accel outages allow prediction; do not advertise an indefinitely
     * uncorrected orientation when the accelerometer has stopped sampling. */
    if ((uint32_t)(now_ms - s_imu_sample_ms[2]) >= RDK_FEEDBACK_MAX_AGE_MS) valid = 0U;
    if (valid != 0U)
    {
        /* Filter already normalized and validated q; do not repeat sqrt/div. */
        memcpy(s_imu, s_attitude.xyzw, sizeof(float) * 4U);
        s_imu_pending_flags |= RDK_UART_ORIENTATION_VALID;
        s_imu_sample_ms[0] = now_ms;
        s_orientation_valid = 1U;
    }
}

void RDK_UART_UpdateOrientation(const float xyzw[4], uint8_t valid)
{
    s_imu_pending_flags &= (uint8_t)~RDK_UART_ORIENTATION_VALID;
    s_orientation_valid = 0U;
    if ((xyzw == NULL) || (valid == 0U)) return;
    if (RDK_NormalizeOrientation(xyzw, s_imu) == 0U) return;
    s_imu_pending_flags |= RDK_UART_ORIENTATION_VALID;
    s_imu_sample_ms[0] = HAL_GetTick();
    s_orientation_valid = 1U;
}

uint8_t RDK_UART_GetOrientation(float xyzw[4])
{
    if ((xyzw == NULL) || (s_orientation_valid == 0U) ||
        ((uint32_t)(HAL_GetTick() - s_imu_sample_ms[0]) >= 50U)) return 0U;
    memcpy(xyzw, s_imu, sizeof(float) * 4U);
    return 1U;
}

void RDK_UART_GetImuDiagnostics(RDK_UART_ImuDiagnostics_t *out)
{
    if (out == NULL) return;
    *out = s_imu_diagnostics;
    uint32_t now = HAL_GetTick();
    out->gyro_age_ms = out->gyro_samples != 0U ? (uint32_t)(now - s_imu_sample_ms[1]) : UINT32_MAX;
    out->accel_age_ms = out->accel_samples != 0U ? (uint32_t)(now - s_imu_sample_ms[2]) : UINT32_MAX;
    out->bias_valid = s_bias_valid;
    out->initialized = s_attitude.initialized;
    out->alignment_samples = s_attitude.alignment_samples;
    out->orientation_valid = (uint8_t)(s_orientation_valid != 0U &&
        (uint32_t)(now - s_imu_sample_ms[0]) < 50U);
}

uint8_t RDK_UART_IsCommandReady(void)
{
    uint32_t mask = RDK_Lock();
    uint8_t ready = (uint8_t)((s_command_ready != 0U) &&
        ((uint32_t)(HAL_GetTick() - s_last_command_ms) < RDK_UART_COMMAND_TIMEOUT_MS));
    RDK_Unlock(mask);
    return ready;
}

void RDK_UART_GetStatistics(RDK_UART_Statistics_t *statistics)
{
    if (statistics == NULL) return;
    uint32_t mask = RDK_Lock();
    *statistics = s_statistics;
    statistics->rx_restart_errors = s_rx.restart_errors;
    statistics->rx_overruns = s_rx.overruns;
    RDK_Unlock(mask);
}
