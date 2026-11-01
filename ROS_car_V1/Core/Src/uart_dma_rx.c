#include "uart_dma_rx.h"
#include <string.h>

static uint32_t Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

static void ResetPositions(UART_DMA_RX_t *rx)
{
    rx->produced = 0U;
    rx->consumed = 0U;
    rx->event_head = 0U;
    rx->event_tail = 0U;
    rx->last_position = 0U;
}

HAL_StatusTypeDef UART_DMA_ResetStopped(DMA_HandleTypeDef *dma)
{
    if ((dma->Instance->CR & DMA_SxCR_EN) != 0U)
    {
        (void)HAL_DMA_Abort(dma);
        if ((dma->Instance->CR & DMA_SxCR_EN) != 0U) return HAL_BUSY;
    }
    /* A previous HAL abort timeout has already cleared DMAR/DMAT. Retrying
     * UART_Abort alone then returns OK but leaves DMA.State == TIMEOUT. */
    if (dma->State != HAL_DMA_STATE_READY)
    {
        if (HAL_DMA_DeInit(dma) != HAL_OK) return HAL_ERROR;
        return HAL_DMA_Init(dma);
    }
    return HAL_OK;
}

static HAL_StatusTypeDef Start(UART_DMA_RX_t *rx)
{
    uint32_t mask = Lock();
    ResetPositions(rx);
    rx->restart = 0U;
    __HAL_UART_CLEAR_OREFLAG(rx->uart);
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(rx->uart, rx->data, UART_DMA_RX_CAPACITY);
    if (status != HAL_OK)
    {
        rx->restart = 1U;
        ++rx->restart_errors;
    }
    __set_PRIMASK(mask);
    return status;
}

HAL_StatusTypeDef UART_DMA_RX_Init(UART_DMA_RX_t *rx, UART_HandleTypeDef *uart)
{
    if ((rx == NULL) || (uart == NULL) || (uart->hdmarx == NULL)) return HAL_ERROR;
    memset(rx, 0, sizeof(*rx));
    rx->uart = uart;
    rx->last_restart_ms = HAL_GetTick();
    rx->attempted = 1U;
    return Start(rx);
}

void UART_DMA_RX_Event(UART_DMA_RX_t *rx)
{
    if ((rx == NULL) || (rx->uart == NULL)) return;
    /* UART, DMA and 1 ms timestamp snapshots share this constant-time producer. */
    uint32_t mask = Lock();
    if ((rx->restart != 0U) || (rx->uart->ErrorCode != HAL_UART_ERROR_NONE) ||
        (rx->uart->RxState != HAL_UART_STATE_BUSY_RX))
    {
        __set_PRIMASK(mask);
        return;
    }
    uint16_t remaining = (uint16_t)__HAL_DMA_GET_COUNTER(rx->uart->hdmarx);
    uint16_t position = (uint16_t)(UART_DMA_RX_CAPACITY - remaining);
    if (position == UART_DMA_RX_CAPACITY) position = 0U;
    uint16_t delta = (uint16_t)((position + UART_DMA_RX_CAPACITY - rx->last_position) % UART_DMA_RX_CAPACITY);
    if (delta == 0U) { __set_PRIMASK(mask); return; }
    rx->last_position = position;
    rx->produced += delta;
    if ((uint32_t)(rx->event_head - rx->event_tail) >= UART_DMA_RX_EVENTS)
    {
        ++rx->overruns;
        rx->restart = 1U;
        __set_PRIMASK(mask);
        return;
    }
    UART_DMA_RX_Event_t *event = &rx->events[rx->event_head % UART_DMA_RX_EVENTS];
    event->end = rx->produced;
    event->received_ms = HAL_GetTick();
    __DMB();
    ++rx->event_head;
    __set_PRIMASK(mask);
}

void UART_DMA_RX_Error(UART_DMA_RX_t *rx)
{
    if ((rx == NULL) || (rx->uart == NULL)) return;
    ++rx->errors;
    rx->restart = 1U;
}

uint8_t UART_DMA_RX_Service(UART_DMA_RX_t *rx)
{
    if ((rx == NULL) || (rx->uart == NULL)) return 0U;
    if ((rx->restart == 0U) && (rx->uart->RxState != HAL_UART_STATE_READY)) return 0U;
    rx->restart = 1U;
    uint32_t now = HAL_GetTick();
    if ((rx->attempted != 0U) && ((uint32_t)(now - rx->last_restart_ms) < 10U)) return 1U;
    rx->last_restart_ms = now;
    rx->attempted = 1U;
    /* HAL abort may poll for DMA disable: never run it from an ISR/PRIMASK. */
    if (HAL_UART_AbortReceive(rx->uart) != HAL_OK)
    {
        rx->restart = 1U;
        ++rx->restart_errors;
        return 1U;
    }
    if (UART_DMA_ResetStopped(rx->uart->hdmarx) != HAL_OK)
    {
        ++rx->restart_errors;
        return 1U;
    }
    (void)Start(rx);
    return 1U;
}

void UART_DMA_RX_Discard(UART_DMA_RX_t *rx)
{
    uint32_t mask = Lock();
    UART_DMA_RX_Event(rx);
    rx->consumed = rx->produced;
    rx->event_tail = rx->event_head;
    __set_PRIMASK(mask);
}

int UART_DMA_RX_Read(UART_DMA_RX_t *rx, uint8_t *byte, uint32_t *received_ms)
{
    uint32_t mask = Lock();
    if ((rx->restart != 0U) ||
        ((uint32_t)(rx->produced - rx->consumed) > UART_DMA_RX_CAPACITY / 2U))
    {
        if (rx->restart == 0U) ++rx->overruns;
        rx->consumed = rx->produced;
        rx->event_tail = rx->event_head;
        __set_PRIMASK(mask);
        return -1;
    }
    if (rx->event_tail == rx->event_head)
    {
        __set_PRIMASK(mask);
        return 0;
    }
    const UART_DMA_RX_Event_t *event = &rx->events[rx->event_tail % UART_DMA_RX_EVENTS];
    *received_ms = event->received_ms;
    *byte = rx->data[rx->consumed % UART_DMA_RX_CAPACITY];
    ++rx->consumed;
    if (rx->consumed == event->end) ++rx->event_tail;
    __set_PRIMASK(mask);
    return 1;
}
