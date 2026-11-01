#ifndef UART_DMA_RX_H
#define UART_DMA_RX_H
#include "main.h"
#ifdef __cplusplus
extern "C" {
#endif

#define UART_DMA_RX_CAPACITY 1024U
#define UART_DMA_RX_EVENTS 64U
#define UART_DMA_RX_PARSE_BYTES 128U
#define UART_DMA_RX_PARSE_US 500U

typedef struct { uint32_t end, received_ms; } UART_DMA_RX_Event_t;
typedef struct
{
    UART_HandleTypeDef *uart;
    uint8_t data[UART_DMA_RX_CAPACITY];
    UART_DMA_RX_Event_t events[UART_DMA_RX_EVENTS];
    volatile uint32_t produced, event_head;
    uint32_t consumed;
    volatile uint32_t event_tail;
    uint16_t last_position;
    volatile uint8_t restart;
    uint8_t attempted;
    uint32_t last_restart_ms;
    volatile uint32_t errors, overruns;
    uint32_t restart_errors;
} UART_DMA_RX_t;

/* RX DMA must be CIRCULAR with both HT and TC interrupts retained. */
HAL_StatusTypeDef UART_DMA_RX_Init(UART_DMA_RX_t *rx, UART_HandleTypeDef *uart);
/* Main-only, AFTER disabling the UART DMA request. Verify physical stop and
 * repair a HAL DMA handle left BUSY/TIMEOUT by an interrupted/failed abort. */
HAL_StatusTypeDef UART_DMA_ResetStopped(DMA_HandleTypeDef *dma);
/* ISR: capture current NDTR position/time only. Size from HAL is deliberately
 * not used: delayed HT/IDLE callbacks may otherwise report an older position. */
void UART_DMA_RX_Event(UART_DMA_RX_t *rx);
void UART_DMA_RX_Error(UART_DMA_RX_t *rx);
/* Main: recover/arm reception. Returns 1 when the parser must discard its
 * partial frame. Abort/restart is deferred out of interrupt context. */
uint8_t UART_DMA_RX_Service(UART_DMA_RX_t *rx);
/* Main-only takeover barrier: discard queued bytes including current NDTR.
 * DMA keeps running; the next command must begin after this boundary. */
void UART_DMA_RX_Discard(UART_DMA_RX_t *rx);
/* Main: 1=byte, 0=none, -1=gap/error; received_ms is the original DMA event
 * time, never the time background parsing happens. Consumer retains half
 * the buffer as a guard for the as-yet-unreported DMA half-block. */
int UART_DMA_RX_Read(UART_DMA_RX_t *rx, uint8_t *byte, uint32_t *received_ms);
#ifdef __cplusplus
}
#endif
#endif
