#ifndef RDK_UART_H
#define RDK_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "IMU.h"
#include <stddef.h>

/* Compile-time availability of host drive support. Runtime ownership is
 * selected by drive_control.c / radio CH6; feedback is always transmitted. */
#ifndef RDK_UART_CONTROL_ENABLE
#define RDK_UART_CONTROL_ENABLE 1U
#endif

/* Match the existing odometry and 10000 cps mecanum limit. Calibrate on hardware. */
#define RDK_UART_COUNTS_PER_WHEEL_REV 1333.0f
#define RDK_UART_MAX_SPEED_RAD_S (10000.0f * 6.283185307f / RDK_UART_COUNTS_PER_WHEEL_REV)
#define RDK_UART_COMMAND_TIMEOUT_MS 200U
#define RDK_UART_FEEDBACK_PERIOD_MS 20U
#define RDK_UART_SPEED_FRAME_SIZE 21U
#define RDK_UART_WHEEL_FRAME_SIZE 22U
#define RDK_UART_IMU_FRAME_SIZE 46U
#define RDK_UART_ALL_WHEELS_VALID 0x0FU
#define RDK_UART_ORIENTATION_VALID 0x01U
#define RDK_UART_GYRO_VALID 0x02U
#define RDK_UART_ACCEL_VALID 0x04U

typedef struct
{
    uint32_t command_frames;
    uint32_t crc_errors;
    uint32_t invalid_commands;
    uint32_t commands_before_zero;
    uint32_t command_timeouts;
    uint32_t uart_errors;
    uint32_t rx_restart_errors;
    uint32_t tx_frames;
    uint32_t tx_errors;
    uint32_t rx_overruns;
    uint32_t stale_commands;
    uint32_t commands_while_disabled;
} RDK_UART_Statistics_t;

typedef struct
{
    uint32_t gyro_samples, accel_samples;
    uint32_t gyro_age_ms, accel_age_ms, max_gyro_gap_ms;
    uint32_t gyro_gap_resets, alignment_resets;
    uint16_t alignment_samples;
    uint8_t bias_valid, initialized, orientation_valid, last_fresh_flags;
} RDK_UART_ImuDiagnostics_t;

/* Call once after Motor/IMU/UART initialization, before enabling normal drive.
 * Port must be 115200, 8N1, RX circular DMA and TX normal DMA, IRQs enabled. */
HAL_StatusTypeDef RDK_UART_Init(UART_HandleTypeDef *huart);

/* Main loop only: starts one persistent 68-byte DMA transmission every 20 ms.
 * No printf/blocking transmit/SPI access. RX processing can force a watchdog stop. */
void RDK_UART_Process(uint32_t now_ms);
/* May also be serviced before/after other background tasks. Max 128 bytes
 * and 500 us per call (checked after each byte/frame), no parsing in ISR. */
void RDK_UART_PollRx(void);

/* Called only by the drive owner in the 20 ms timer ISR, before the motor PID.
 * Applies all four host targets together and checks its own watchdog. */
/* Return 0 while locked: update encoders, but do NOT run the motor PID. */
uint8_t RDK_UART_ControlTick(uint32_t now_ms);
/* Runtime grant. Revoking/renewing clears the zero handshake and requests
 * a main-loop RX discard barrier. Safe from watchdog ISR; no HAL polling. */
void RDK_UART_SetControlEnabled(uint8_t enabled);
/* Arbiter-authorized host control: discard old RX data, then accept the next
 * valid fresh command, including nonzero. Timeout/error clears targets; a new
 * valid command may resume without a host-side zero handshake. Repeated grants
 * do not discard data or clear a live command. */
void RDK_UART_GrantFreshControl(void);
uint8_t RDK_UART_IsControlEnabled(void);

/* Delegated by DriveControl_WatchdogTick when host owns the motors. At 200 ms,
 * clear targets/PID/PWM; reacquisition follows the grant policy above.
 * Inactive host cannot stop radio. */
void RDK_UART_WatchdogTick(uint32_t now_ms);

/* Forward HAL callbacks here. Other UART callbacks remain in main.c. */
void RDK_UART_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position);
/* 1 ms position/time snapshot also bounds timestamps for gapless RX streams. */
void RDK_UART_RxTick(void);
void RDK_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void RDK_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* Main loop only. LSM6DSR configuration: +/-250 dps, +/-2 g.
 * fresh_flags uses IMU_MOTION_GYRO_FRESH / IMU_MOTION_ACCEL_FRESH.
 * Pass flags=0 on read/init failure; unavailable groups are cleared immediately.
 * Data stays in the sensor's right-handed axes. Acceleration retains gravity. */
/* Supply a successful stationary three-axis calibration once after Init.
 * Until supplied, automatic quaternion output stays invalid. Changing bias
 * resets stationary alignment. NaN/Inf disables automatic attitude output. */
void RDK_UART_SetGyroBiasRaw(float x, float y, float z);
/* Converts new raw data and automatically runs the Mahony six-axis filter.
 * Initialization needs 32 near-static samples; a >50 ms gyro gap realigns.
 * Untransmitted fresh groups accumulate until TX so separate DRDY phases
 * cannot erase each other; fusion uses only this call's fresh samples. */
void RDK_UART_UpdateImuRaw(const IMU_AxisRaw_t *gyro,
                           const IMU_AxisRaw_t *accel,
                           uint8_t fresh_flags);

/* Optional external AHRS override for the current sample, main loop only.
 * Normal operation calls this internally after Mahony. xyzw maps sensor ->
 * z-up world, initial relative yaw=0; norm must be within 0.01 of 1. */
void RDK_UART_UpdateOrientation(const float xyzw[4], uint8_t valid);
/* Latest valid xyzw, independent of whether that sample was already sent.
 * Returns 0 on initialization/read failure or if the estimate is >=50 ms old. */
uint8_t RDK_UART_GetOrientation(float xyzw[4]);

uint8_t RDK_UART_IsCommandReady(void);
void RDK_UART_GetStatistics(RDK_UART_Statistics_t *statistics);
/* Main-only local diagnostics; UINT32_MAX age means no sample yet.
 * This API adds no bytes or debug text to the USART3 wire protocol. */
void RDK_UART_GetImuDiagnostics(RDK_UART_ImuDiagnostics_t *diagnostics);

/* Byte encoders exposed for serial-tool tests. Input arrays are FL,FR,RL,RR
 * and qx,qy,qz,qw,gx,gy,gz,ax,ay,az. Invalid groups are zero-filled.
 * These functions only pack bytes; use Process() for scheduled transmission. */
uint16_t RDK_UART_Crc16(const uint8_t *data, size_t size);
void RDK_UART_PackWheelFeedback(const float velocity[4], uint8_t valid_flags,
                                uint8_t frame[RDK_UART_WHEEL_FRAME_SIZE]);
void RDK_UART_PackImuFeedback(const float values[10], uint8_t valid_flags,
                              uint8_t frame[RDK_UART_IMU_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif
#endif
