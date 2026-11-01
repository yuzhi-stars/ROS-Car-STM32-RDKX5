#ifndef DRIVE_CONTROL_H
#define DRIVE_CONTROL_H
#include "main.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { DRIVE_STOP, DRIVE_HOST, DRIVE_REMOTE } Drive_Mode_t;
#define DRIVE_RADIO_TIMEOUT_MS 300U
#define DRIVE_REMOTE_WHEEL_LIMIT_CPS 1000L

/* One motor owner. CH6 low=host, middle=stop, high=remote; CH5 master arm
 * once a radio has been seen. HOST with CH5 high accepts fresh commands without
 * another arm cycle or zero handshake. REMOTE still requires rearm/centering;
 * radio loss requires rearm in either mode. No radio retains standalone host.
 * Init after RDK/ELRS and Motor init. Process after ELRS_Poll in MAIN. */
void DriveControl_Init(void);
void DriveControl_Process(void);
/* Called before the motor PID in TIM13; 0 means encoder sampling only. */
uint8_t DriveControl_ControlTick(uint32_t now_ms);
/* 1 ms watchdog, no parsing or HAL waits. Radio loss never falls back to host. */
void DriveControl_WatchdogTick(uint32_t now_ms);
Drive_Mode_t DriveControl_GetMode(void);
uint8_t DriveControl_IsEnabled(void);
const char *DriveControl_GetLabel(void);
#ifdef __cplusplus
}
#endif
#endif
