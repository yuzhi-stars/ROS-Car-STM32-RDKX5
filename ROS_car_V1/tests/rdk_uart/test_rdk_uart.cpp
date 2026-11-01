#include "rdk_uart.h"
#include "imu_attitude.h"
#include "imu_calibration.h"
#include "uart_dma_rx.h"
#include "x5car_protocol.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (0)

static uint32_t tick, mask, encoder_tick, stops, transmissions;
static DMA_HandleTypeDef rx_dma{}, tx_dma{};
static DMA_Stream_TypeDef tx_stream{}, rx_stream{};
static uint32_t tx_aborts, dma_reinitializations;
static bool abort_tx_fails;
static UART_HandleTypeDef uart{0,0,0,&rx_dma,&tx_dma};
static uint8_t *rx_data, *tx_data;
static uint16_t tx_size;
static uint16_t rx_size, rx_position;
static bool exhaust_budget, preempt_next_sample;
static std::array<int32_t, 4> targets{}, encoders{};
static HAL_StatusTypeDef next_rx_status = HAL_OK, next_tx_status = HAL_OK;

extern "C" {
uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1; }
void __set_PRIMASK(uint32_t value) { mask = value; }
uint32_t RuntimeClock_Cycles(void) { return 0; }
uint8_t RuntimeClock_BudgetExpired(uint32_t, uint32_t) { return exhaust_budget; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *port, uint8_t *data, uint16_t size)
{
    CHECK(port == &uart && size == UART_DMA_RX_CAPACITY);
    if (next_rx_status != HAL_OK)
    {
        auto result = next_rx_status;
        next_rx_status = HAL_OK;
        return result;
    }
    if (port->RxState != HAL_UART_STATE_READY) return HAL_BUSY;
    rx_data = data;
    rx_size = size;
    rx_position = 0;
    rx_dma.remaining = size;
    port->RxState = HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *port, uint8_t *data, uint16_t size)
{
    CHECK(port == &uart);
    if (next_tx_status != HAL_OK)
    {
        auto result = next_tx_status;
        next_tx_status = HAL_OK;
        return result;
    }
    CHECK(port->gState == HAL_UART_STATE_READY);
    port->gState = HAL_UART_STATE_BUSY_TX;
    CHECK(tx_dma.State==HAL_DMA_STATE_READY);
    tx_stream.CR = DMA_SxCR_EN; tx_dma.State=HAL_DMA_STATE_BUSY;
    tx_data = data;
    tx_size = size;
    ++transmissions;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *port)
{
    CHECK(mask == 0); /* Potentially polling HAL abort must stay out of PRIMASK. */
    port->RxState = HAL_UART_STATE_READY;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *port)
{
    CHECK(mask == 0);
    ++tx_aborts;
    if (abort_tx_fails) { tx_dma.State=HAL_DMA_STATE_TIMEOUT; return HAL_TIMEOUT; }
    tx_stream.CR = 0;
    if (tx_dma.State!=HAL_DMA_STATE_TIMEOUT) tx_dma.State=HAL_DMA_STATE_READY;
    port->gState = HAL_UART_STATE_READY;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma)
{
    CHECK(mask == 0);
    if (abort_tx_fails) return HAL_TIMEOUT;
    dma->Instance->CR = 0; dma->State=HAL_DMA_STATE_READY;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *dma)
{
    CHECK(mask==0 && dma->Instance->CR==0); return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma)
{
    CHECK(mask==0 && dma->Instance->CR==0); dma->State=HAL_DMA_STATE_READY;
    ++dma_reinitializations; return HAL_OK;
}
void Motor_ClosedLoopEmergencyStop(void)
{
    CHECK(mask == 1);
    targets.fill(0);
    ++stops;
}
void Motor_ClosedLoopSetTarget(Motor_ID_t motor, int32_t target)
{
    CHECK(mask == 1);
    targets.at(motor) = target;
}
void Motor_GetAllEncoderSpeeds(int32_t speeds[4])
{
    CHECK(mask == 1);
    std::memcpy(speeds, encoders.data(), sizeof(int32_t) * 4);
}
uint32_t Motor_GetEncoderSampleTick(void)
{
    if (preempt_next_sample) { ++tick; encoder_tick = tick; preempt_next_sample = false; }
    return encoder_tick;
}
}

static void receive_dma(const uint8_t *data, size_t size, bool idle = true)
{
    for (size_t i = 0; i < size; ++i)
    {
        CHECK(uart.RxState == HAL_UART_STATE_BUSY_RX);
        rx_data[rx_position++] = data[i];
        rx_dma.remaining = rx_size - rx_position;
        if (rx_position == rx_size/2) RDK_UART_RxEventCallback(&uart, rx_position);
        if (rx_position == rx_size)
        {
            RDK_UART_RxEventCallback(&uart, rx_position);
            rx_position = 0;
            rx_dma.remaining = rx_size;
        }
    }
    if (idle) RDK_UART_RxEventCallback(&uart, rx_position);
}

static void feed(const uint8_t *data, size_t size)
{
    receive_dma(data, size);
    RDK_UART_PollRx();
}

template<class T> static void feed(const T &data) { feed(data.data(), data.size()); }

static auto command(const x5car::WheelVelocities &speed)
{
    x5car::SerialProtocol::SpeedCommandFrame frame{};
    CHECK(x5car::SerialProtocol::encode_speed_command(speed, 1000.0, frame));
    return frame;
}

static void re_crc(x5car::SerialProtocol::SpeedCommandFrame &frame)
{
    auto crc = x5car::SerialProtocol::crc16_ccitt_false(frame.data() + 2, 17);
    frame[19] = static_cast<uint8_t>(crc);
    frame[20] = static_cast<uint8_t>(crc >> 8);
}

static std::vector<uint8_t> hex(const char *text)
{
    std::istringstream input(text);
    unsigned int value;
    std::vector<uint8_t> result;
    while (input >> std::hex >> value) result.push_back(static_cast<uint8_t>(value));
    return result;
}

static void finish_tx(void)
{
    CHECK(uart.gState == HAL_UART_STATE_BUSY_TX);
    uart.gState = HAL_UART_STATE_READY;
    tx_stream.CR = 0; tx_dma.State=HAL_DMA_STATE_READY;
    RDK_UART_TxCpltCallback(&uart);
}

int main()
{
    tx_dma.Instance = &tx_stream;
    rx_dma.Instance = &rx_stream;
    const auto zero = command({0, 0, 0, 0});
    const auto moving = command({1, -2, 3, -4});
    CHECK(RDK_UART_Crc16(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0x29B1);
    CHECK(RDK_UART_Crc16(nullptr, 0) == 0xFFFF);
    CHECK(RDK_UART_Crc16(nullptr, 1) == 0);
    CHECK(std::vector<uint8_t>(zero.begin(), zero.end()) == hex(
        "AA 55 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CA F6"));

    std::array<uint8_t, 22> wheel{};
    std::array<uint8_t, 46> imu{};
    float wheel_values[] = {1, -2, 3, -4};
    float imu_values[] = {0, 0, 0, 1, 0, 0, 0, 0, 0, 9.81f};
    RDK_UART_PackWheelFeedback(wheel_values, 0xFF, wheel.data());
    RDK_UART_PackImuFeedback(imu_values, 0xFF, imu.data());
    CHECK(std::vector<uint8_t>(wheel.begin(), wheel.end()) == hex(
        "AA 55 90 00 00 80 3F 00 00 00 C0 00 00 40 40 00 00 80 C0 0F 3F 39"));
    CHECK(std::vector<uint8_t>(imu.begin(), imu.end()) == hex(
        "AA 55 91 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 3F "
        "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 C3 F5 1C 41 07 24 9A"));
    x5car::SerialProtocol host;
    size_t decoded = 0;
    for (auto byte : wheel) decoded += host.feed(&byte, 1);
    for (auto byte : imu) decoded += host.feed(&byte, 1);
    CHECK(decoded == 2 && host.wheel_feedback_usable() && host.imu_feedback_usable());
    CHECK(host.wheel_feedback().velocity[3] == -4);
    std::puts("PASS golden frames and original upstream parser interoperability");

    wheel_values[1] = std::numeric_limits<float>::quiet_NaN();
    RDK_UART_PackWheelFeedback(wheel_values, 0x0F, wheel.data());
    CHECK(wheel[19] == 0x0D && wheel[7] == 0 && wheel[10] == 0);
    imu_values[3] = 2;
    imu_values[5] = std::numeric_limits<float>::infinity();
    RDK_UART_PackImuFeedback(imu_values, 7, imu.data());
    CHECK(imu[43] == 4 && imu[18] == 0 && imu[26] == 0);
    std::puts("PASS nonfinite feedback, quaternion validation and reserved-bit masking");

    CHECK(RDK_UART_Init(nullptr) == HAL_ERROR);
    CHECK(RDK_UART_Init(&uart) == HAL_OK);
    CHECK(!RDK_UART_IsCommandReady() && !RDK_UART_ControlTick(tick));
    feed(moving);
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    CHECK(RDK_UART_IsCommandReady());
    feed(moving.data(), 8);
    CHECK(RDK_UART_ControlTick(tick) && targets[0] == 0);
    feed(moving.data() + 8, moving.size() - 8);
    CHECK(RDK_UART_ControlTick(tick));
    for (size_t i = 0; i < 4; ++i)
        CHECK(targets[i] == std::lround(static_cast<float>(i + 1) * (i % 2 ? -1 : 1) *
              RDK_UART_COUNTS_PER_WHEEL_REV / 6.283185307f));
    const auto saved_targets = targets;

    tick = 100;
    auto bad_crc = moving;
    bad_crc[10] ^= 0x80;
    feed(bad_crc);
    auto invalid = command({100, 0, 0, 0});
    feed(invalid);
    for (uint32_t bits : {0x7FC00000U, 0x7F800000U, 0xFF800000U})
    {
        invalid = zero;
        for (size_t i = 0; i < 4; ++i) invalid[3 + i] = static_cast<uint8_t>(bits >> (8 * i));
        re_crc(invalid);
        feed(invalid);
    }
    CHECK(RDK_UART_ControlTick(tick) && targets == saved_targets);
    tick = 199;
    CHECK(RDK_UART_ControlTick(tick));
    tick = 200;
    RDK_UART_WatchdogTick(tick);  /* 1 ms watchdog stops even without the motor tick. */
    CHECK(targets == (std::array<int32_t, 4>{}) && !RDK_UART_IsCommandReady());
    CHECK(!RDK_UART_ControlTick(tick) && targets == (std::array<int32_t, 4>{}));
    feed(moving);
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    feed(moving);
    CHECK(RDK_UART_IsCommandReady());
    std::puts("PASS zero handshake, fragmented commands, CRC/NaN/Inf/range rejection and watchdog");

    tick = 400;  /* Late command must not bypass timeout before the control tick. */
    feed(moving);
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    const uint8_t noise[] = {0, 0xAA, 0xAA, 0x55, 0x12, 0xAA};
    feed(noise, sizeof(noise));
    feed(moving);
    CHECK(RDK_UART_ControlTick(tick) && targets == saved_targets);
    uint32_t special_bits = 0x3F55AA00U;
    float special;
    std::memcpy(&special, &special_bits, sizeof(special));
    feed(command({special, 0, 0, 0}));  /* AA 55 inside float data. */
    CHECK(RDK_UART_ControlTick(tick));
    CHECK(targets[0] == std::lround(special * RDK_UART_COUNTS_PER_WHEEL_REV / 6.283185307f));
    tick = 599;
    feed(wheel);
    feed(imu);
    tick = 600;
    CHECK(!RDK_UART_ControlTick(tick));  /* Feedback echoes cannot keep motion alive. */
    feed(moving.data(), 8);
    feed(zero);  /* Recover a valid frame nested inside a corrupted candidate. */
    CHECK(RDK_UART_IsCommandReady());
    std::puts("PASS timeout race, noise, payload headers, echoes and truncated-frame recovery");

    tick = 610;
    feed(zero.data(), zero.size() - 1);
    uart.ErrorCode = HAL_UART_ERROR_ORE;
    feed(zero.data() + zero.size() - 1, 1);
    RDK_UART_ErrorCallback(&uart);
    CHECK(!RDK_UART_IsCommandReady());
    tick += 10;
    RDK_UART_PollRx();
    feed(moving);
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    next_rx_status = HAL_ERROR;
    RDK_UART_ErrorCallback(&uart);
    tick += 10;
    RDK_UART_PollRx();
    CHECK(uart.RxState == HAL_UART_STATE_READY);
    tick += 10;
    RDK_UART_Process(tick);
    CHECK(uart.RxState == HAL_UART_STATE_BUSY_RX);
    finish_tx();
    std::puts("PASS UART overrun, bad-byte rejection and failed RX-arm recovery");

    tick = 1000;
    encoder_tick = 990;
    encoders = {1333, -1333, 0, 2666};
    IMU_AxisRaw_t gyro{110, 220, 330}, accel{0, 0, 16384};
    float identity[] = {0, 0, 0, 1};
    RDK_UART_SetGyroBiasRaw(10, 20, 30);
    RDK_UART_UpdateImuRaw(&gyro, &accel, 3);
    RDK_UART_UpdateOrientation(identity, 1);
    RDK_UART_Process(tick);
    CHECK(tx_size == 68);
    host.reset();
    CHECK(host.feed(tx_data, tx_size) == 2);
    CHECK(host.wheel_feedback_usable() && host.imu_feedback_usable());
    CHECK(std::abs(host.wheel_feedback().velocity[0] - 6.283185307) < 1e-5);
    CHECK(std::abs(host.imu_feedback().angular_velocity[2] - 300 * 0.00875 * 0.01745329252) < 1e-6);
    CHECK(std::abs(host.imu_feedback().linear_acceleration[2] - 16384 * 0.000061 * 9.80665) < 1e-5);
    auto in_flight = std::vector<uint8_t>(tx_data, tx_data + tx_size);
    const auto previous_transmissions = transmissions;
    tick = 1020;
    RDK_UART_Process(tick);
    CHECK(transmissions == previous_transmissions);
    CHECK(std::vector<uint8_t>(tx_data, tx_data + tx_size) == in_flight);
    finish_tx();
    RDK_UART_Process(tick);
    CHECK(tx_data[19] == 0 && tx_data[22 + 43] == 0);  /* No sample reused as valid. */
    finish_tx();

    tick = 1040;
    encoder_tick = tick;
    RDK_UART_UpdateImuRaw(&gyro, &accel, 3);
    next_tx_status = HAL_BUSY;
    RDK_UART_Process(tick);
    tick = 1060;
    RDK_UART_Process(tick);
    CHECK(tx_data[19] == 0x0F && tx_data[65] == 6);
    finish_tx();
    tick = 1260;
    encoder_tick = 1050;  /* New relative to last send, but already too old. */
    RDK_UART_UpdateImuRaw(&gyro, &accel, 3);
    tick = 1460;
    RDK_UART_Process(tick);
    CHECK(tx_data[19] == 0 && tx_data[65] == 0);
    finish_tx();
    std::puts("PASS nonblocking TX lifetime, unit conversion, freshness, stale samples and TX retry");

    /* Feed real raw samples through the automatic Mahony path, without the
     * external orientation override, then parse the actual serial TX buffer. */
    IMU_AxisRaw_t resting_gyro{10,20,30};
    float quaternion[4];
    for (unsigned int i = 0; i <= IMU_ATTITUDE_INIT_SAMPLES; ++i)
    {
        tick = 1500 + 10*i;
        encoder_tick = tick;
        RDK_UART_UpdateImuRaw(&resting_gyro, &accel, 3);
        CHECK(RDK_UART_GetOrientation(quaternion) == (i == IMU_ATTITUDE_INIT_SAMPLES));
        if (i % 2 == 0)
        {
            RDK_UART_Process(tick);
            CHECK(tx_data[65] == (i == IMU_ATTITUDE_INIT_SAMPLES ? 7 : 6));
            finish_tx();
        }
    }
    CHECK(std::abs(quaternion[3] - 1) < 1e-6);
    IMU_AxisRaw_t yawing_gyro = resting_gyro;
    yawing_gyro.z += static_cast<int16_t>(std::lround((3.141592653589793/2) /
                                                   (0.00875 * 0.01745329252)));
    for (unsigned int i = 0; i < 100; ++i)
    {
        tick += 10;
        encoder_tick = tick;
        RDK_UART_UpdateImuRaw(&yawing_gyro, &accel, 3);
    }
    RDK_UART_Process(tick);
    host.reset();
    CHECK(host.feed(tx_data, tx_size) == 2 && host.imu_feedback_usable());
    auto q = host.imu_feedback().orientation;
    CHECK(std::abs(q[0]) < 1e-5 && std::abs(q[1]) < 1e-5);
    CHECK(std::abs(q[2] - std::sqrt(.5)) < .0001 && std::abs(q[3] - std::sqrt(.5)) < .0001);
    CHECK(tx_data[65] == 7);
    finish_tx();
    tick += 20;
    RDK_UART_UpdateImuRaw(&resting_gyro, nullptr, IMU_MOTION_GYRO_FRESH);
    CHECK(RDK_UART_GetOrientation(quaternion));
    RDK_UART_Process(tick);
    CHECK(tx_data[65] == 3);  /* Fresh gyro prediction, acceleration unavailable. */
    finish_tx();
    tick += 20;
    RDK_UART_UpdateImuRaw(nullptr, &accel, IMU_MOTION_ACCEL_FRESH);
    CHECK(RDK_UART_GetOrientation(quaternion)); /* Last gyro estimate is <50 ms old. */
    RDK_UART_Process(tick);
    CHECK(tx_data[65] == 4);  /* Accel-only input cannot create a new quaternion. */
    finish_tx();
    tick += 100;
    RDK_UART_UpdateImuRaw(&resting_gyro, &accel, 3);
    CHECK(!RDK_UART_GetOrientation(quaternion));
    for (unsigned int i = 0; i < IMU_ATTITUDE_INIT_SAMPLES; ++i)
    {
        tick += 10;
        RDK_UART_UpdateImuRaw(&resting_gyro, &accel, 3);
    }
    CHECK(RDK_UART_GetOrientation(quaternion));
    CHECK(std::abs(quaternion[3] - 1) < 1e-6);  /* Sampling gap starts a new relative heading. */
    RDK_UART_Process(tick);
    CHECK(tx_data[65] == 7);
    finish_tx();
    tick += 50;
    CHECK(!RDK_UART_GetOrientation(quaternion));
    RDK_UART_Process(tick);
    CHECK(tx_data[65] == 0);  /* No new sampling, no repeated valid attitude. */
    finish_tx();
    RDK_UART_SetGyroBiasRaw(std::numeric_limits<float>::quiet_NaN(),0,0);
    for (unsigned int i = 0; i < 100; ++i)
    {
        tick += 10;
        RDK_UART_UpdateImuRaw(&resting_gyro, &accel, 3);
        CHECK(!RDK_UART_GetOrientation(quaternion));
    }
    std::puts("PASS automatic Mahony-to-serial quaternion, 90-degree yaw, flags 07/03/04, gaps and calibration gating");

    tick = UINT32_MAX - 100U;
    feed(zero);
    feed(moving);
    tick = 98;
    CHECK(RDK_UART_ControlTick(tick));
    tick = 99;
    CHECK(!RDK_UART_ControlTick(tick));
    uint32_t random = 1234567;
    for (size_t i = 0; i < 20000; ++i)
    {
        random = random * 1664525U + 1013904223U;
        uint8_t byte = static_cast<uint8_t>(random >> 24);
        feed(&byte, 1);
    }
    feed(zero);
    std::array<uint8_t, 46> flush{};
    feed(flush);
    CHECK(RDK_UART_IsCommandReady());
    tick += 1000;
    RDK_UART_WatchdogTick(tick);
    receive_dma(zero.data(), zero.size());
    CHECK(!RDK_UART_IsCommandReady()); /* DMA callback has not parsed anything. */
    tick += 201;
    RDK_UART_PollRx();
    CHECK(!RDK_UART_IsCommandReady()); /* Old queue cannot refresh/rearm motion. */
    receive_dma(zero.data(), zero.size());
    exhaust_budget = true;
    RDK_UART_PollRx();
    CHECK(!RDK_UART_IsCommandReady());
    exhaust_budget = false;
    RDK_UART_PollRx();
    CHECK(RDK_UART_IsCommandReady());
    std::array<uint8_t, 1024> overflow{};
    receive_dma(overflow.data(), overflow.size());
    RDK_UART_PollRx();
    CHECK(!RDK_UART_IsCommandReady());
    feed(moving);
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    CHECK(RDK_UART_IsCommandReady());
    for (unsigned int i=0; i<70; ++i)
    {
        const uint8_t byte = 0;
        receive_dma(&byte,1); /* More IDLE metadata than the event ring can hold. */
    }
    tick += 10;
    RDK_UART_PollRx();
    CHECK(!RDK_UART_IsCommandReady());
    feed(zero);
    CHECK(RDK_UART_IsCommandReady());
    preempt_next_sample = true;
    encoder_tick = tick - 1;
    RDK_UART_Process(tick);
    CHECK(tx_data[19] == 0x0F); /* Capture time after the simulated timer preemption. */
    finish_tx();
    tick += 201;
    RDK_UART_WatchdogTick(tick);
    receive_dma(zero.data(), zero.size(), false);
    CHECK(!RDK_UART_IsCommandReady());
    RDK_UART_RxTick(); /* A gapless partial DMA half is timestamped by the 1 ms tick. */
    CHECK(!RDK_UART_IsCommandReady());
    RDK_UART_PollRx();
    CHECK(RDK_UART_IsCommandReady());
    std::puts("PASS deferred DMA parsing, timestamp age, work budget, wrap/overflow and snapshot preemption");
    RDK_UART_Statistics_t statistics{};
    RDK_UART_GetStatistics(&statistics);
    CHECK(statistics.crc_errors >= 1 && statistics.invalid_commands >= 4);
    CHECK(statistics.commands_before_zero >= 3 && statistics.command_timeouts >= 4);
    CHECK(statistics.uart_errors >= 2 && statistics.rx_restart_errors == 1);
    CHECK(statistics.tx_errors == 1 && statistics.tx_frames >= 8 && mask == 0);
    CHECK(statistics.stale_commands >= 1 && statistics.rx_overruns >= 2);
    CHECK(stops >= 4);
    std::puts("PASS tick rollover, 20000 noise bytes and diagnostic counters");

    tick+=20; RDK_UART_Process(tick);
    auto original=std::vector<uint8_t>(tx_data,tx_data+tx_size);
    auto sent=transmissions, aborted=tx_aborts;
    tick+=39; RDK_UART_Process(tick);
    CHECK(tx_aborts==aborted && transmissions==sent);
    abort_tx_fails=true; ++tick; RDK_UART_Process(tick);
    CHECK(tx_aborts==aborted+1 && transmissions==sent);
    CHECK(std::vector<uint8_t>(tx_data,tx_data+tx_size)==original && tx_stream.CR!=0);
    RDK_UART_Process(tick); CHECK(tx_aborts==aborted+1); /* Backoff. */
    abort_tx_fails=false; tick+=10; RDK_UART_Process(tick);
    CHECK(transmissions==sent+1 && tx_aborts==aborted+2);
    finish_tx();

    tick+=20; RDK_UART_Process(tick);
    original.assign(tx_data,tx_data+tx_size); sent=transmissions; aborted=tx_aborts;
    /* HAL's RX DMA error also ends UART TX, while its DMA can remain active. */
    uart.ErrorCode=HAL_UART_ERROR_DMA; uart.gState=HAL_UART_STATE_READY;
    uart.RxState=HAL_UART_STATE_READY; RDK_UART_ErrorCallback(&uart);
    CHECK(tx_stream.CR!=0 && tx_aborts==aborted);
    abort_tx_fails=true; tick+=10; RDK_UART_Process(tick);
    CHECK(transmissions==sent && tx_aborts==aborted+1);
    CHECK(std::vector<uint8_t>(tx_data,tx_data+tx_size)==original);
    abort_tx_fails=false; tick+=10; RDK_UART_Process(tick);
    CHECK(transmissions==sent+1 && tx_aborts==aborted+2);
    CHECK(!RDK_UART_IsCommandReady()); finish_tx();
    RDK_UART_GetStatistics(&statistics); CHECK(statistics.tx_errors==3 && mask==0);
    CHECK(dma_reinitializations==2);
    std::puts("PASS lost TX completion, abort failure/backoff, buffer ownership and RX DMA error during TX");

    /* Actual calibration -> RDK bias -> intermittent accel -> AHRS -> wire.
     * Start with a failed/moving window; no validity bit is fabricated. */
    RDK_UART_SetGyroBiasRaw(std::numeric_limits<float>::quiet_NaN(),0,0);
    IMU_Calibration_t calibration;
    IMU_Calibration_Init(&calibration);
    bool published_bias=false, saw_valid_wire=false;
    for (unsigned int i=0;i<400;++i)
    {
        tick+=10; encoder_tick=tick;
        IMU_AxisRaw_t sample{static_cast<int16_t>(i>=60 && i<90 ? 1000 : 10),20,30};
        const float raw[]={static_cast<float>(sample.x),20,30};
        const float accel_g[]={0,0,16384*.000061f};
        uint8_t fresh=i%25==24 ? 2 : 3;
        IMU_Calibration_Update(&calibration,raw,accel_g,fresh,tick,!(i>=30 && i<60));
        if (calibration.valid && !published_bias)
        {
            RDK_UART_SetGyroBiasRaw(calibration.bias_raw[0],calibration.bias_raw[1],calibration.bias_raw[2]);
            published_bias=true;
        }
        RDK_UART_UpdateImuRaw(&sample,&accel,fresh);
        if (!published_bias) CHECK(!RDK_UART_GetOrientation(quaternion));
        if (i%2==1)
        {
            RDK_UART_Process(tick); host.reset(); CHECK(host.feed(tx_data,tx_size)==2);
            if (!published_bias) CHECK((tx_data[65]&1)==0);
            if (host.imu_feedback_usable()) saw_valid_wire=true;
            finish_tx();
        }
    }
    CHECK(published_bias && saw_valid_wire && calibration.rejected_windows>=1);
    RDK_UART_ImuDiagnostics_t diag{}; RDK_UART_GetImuDiagnostics(&diag);
    CHECK(diag.bias_valid && diag.initialized && diag.orientation_valid && diag.alignment_samples==32);
    CHECK(diag.gyro_age_ms==0 && diag.accel_age_ms==10 && diag.last_fresh_flags==2);
    tick+=10; RDK_UART_UpdateImuRaw(&resting_gyro,nullptr,2);
    tick+=10; RDK_UART_UpdateImuRaw(nullptr,&accel,1);
    RDK_UART_Process(tick); host.reset(); CHECK(host.feed(tx_data,tx_size)==2);
    CHECK(tx_data[65]==7 && host.imu_feedback_usable()); finish_tx();
    /* Group accumulation never runs fusion twice on the same gyro sample. */
    tick+=20; RDK_UART_Process(tick); CHECK(tx_data[65]==0); finish_tx();
    auto gap_resets=diag.gyro_gap_resets;
    tick+=51; RDK_UART_UpdateImuRaw(&resting_gyro,&accel,3);
    RDK_UART_GetImuDiagnostics(&diag);
    CHECK(diag.gyro_gap_resets==gap_resets+1 && diag.max_gyro_gap_ms>=51 && !diag.orientation_valid);
    RDK_UART_GetImuDiagnostics(nullptr);
    std::puts("PASS failed calibration retry, wheel-motion pause, interleaved accel alignment, flags 06->07 and IMU diagnostics");
    std::puts("ALL RDK UART TESTS PASSED");
}
