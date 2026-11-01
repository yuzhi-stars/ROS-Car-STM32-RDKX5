#ifndef __ELRS_H
#define __ELRS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define ELRS_CHANNEL_COUNT 16U

typedef struct
{
  int32_t vx_cps;
  int32_t vy_cps;
  int32_t wz_cps;
  uint8_t link_active;
  uint8_t armed;
  uint8_t alarm_active;
} ELRS_Control_t;

typedef struct
{
  ELRS_Control_t control;
  uint16_t channels[ELRS_CHANNEL_COUNT];
  uint32_t received_ms, sequence;
} ELRS_Snapshot_t;
/* Main-only coherent parsed input. Returns 0 on timeout/RX error. */
uint8_t ELRS_GetSnapshot(ELRS_Snapshot_t *snapshot);

void ELRS_Init(UART_HandleTypeDef *huart);
/* Circular DMA callbacks only capture RX positions/time. Poll in main even
 * when RDK owns the motors; Poll never writes motor targets. */
void ELRS_RxEventCallback(UART_HandleTypeDef *huart, uint16_t position);
void ELRS_RxTick(void);
void ELRS_ErrorCallback(UART_HandleTypeDef *huart);
void ELRS_Poll(void);
typedef struct
{
  uint32_t uart_errors;
  uint32_t rx_overruns;
  uint32_t rx_restart_errors;
} ELRS_Statistics_t;
void ELRS_GetStatistics(ELRS_Statistics_t *statistics);
void ELRS_Process(uint32_t now_ms);
void ELRS_ProcessNoDrive(uint32_t now_ms);
uint8_t ELRS_GetControl(ELRS_Control_t *control);
uint16_t ELRS_GetChannel(uint8_t channel_index);
uint8_t ELRS_IsLinkActive(uint32_t now_ms);
uint8_t ELRS_IsArmed(void);
uint8_t ELRS_IsAlarmActive(void);
uint8_t ELRS_ConsumeArmEvent(void);

#ifdef __cplusplus
}
#endif

#endif /* __ELRS_H */
