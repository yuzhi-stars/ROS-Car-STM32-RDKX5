#include "ELRS.h"
#include "uart_dma_rx.h"
#include <array>
#include <cstdio>
#include <cstdlib>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"ELRS FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
static uint32_t tick, mask, stops, drives, aborts;
static bool budget;
static bool abort_rx_fails;
static DMA_HandleTypeDef dma{};
static DMA_Stream_TypeDef stream{};
static uint32_t dma_reinitializations;
static UART_HandleTypeDef uart{0,0,0,&dma,nullptr};
static uint8_t *buffer;
static uint16_t position;
static HAL_StatusTypeDef next_start=HAL_OK;
extern "C" {
uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t m) { mask=m; }
uint32_t RuntimeClock_Cycles(void) { return 0; }
uint8_t RuntimeClock_BudgetExpired(uint32_t, uint32_t) { return budget; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u, uint8_t *data, uint16_t size)
{
    CHECK(u==&uart && size==UART_DMA_RX_CAPACITY);
    if (next_start!=HAL_OK) { auto s=next_start; next_start=HAL_OK; return s; }
    buffer=data; position=0; dma.remaining=size; u->RxState=HAL_UART_STATE_BUSY_RX;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{
    CHECK(mask==0); ++aborts;
    if (abort_rx_fails) { dma.State=HAL_DMA_STATE_TIMEOUT; return HAL_TIMEOUT; }
    u->RxState=HAL_UART_STATE_READY; return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *d)
{
    CHECK(mask==0);
    if (d->State==HAL_DMA_STATE_TIMEOUT) return HAL_ERROR;
    d->Instance->CR=0; d->State=HAL_DMA_STATE_READY; return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *d)
{
    CHECK(mask==0 && d->Instance->CR==0); return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{
    CHECK(mask==0 && d->Instance->CR==0); d->State=HAL_DMA_STATE_READY;
    ++dma_reinitializations; return HAL_OK;
}
void Mecanum_Stop(void) { ++stops; }
void Mecanum_SetVelocity(int32_t, int32_t, int32_t) { ++drives; }
}
static void receive(const uint8_t *data,size_t size,bool idle=true)
{
    for (size_t i=0;i<size;++i)
    {
        buffer[position++]=data[i]; dma.remaining=UART_DMA_RX_CAPACITY-position;
        if (position==UART_DMA_RX_CAPACITY/2) ELRS_RxEventCallback(&uart,position);
        if (position==UART_DMA_RX_CAPACITY)
        {
            ELRS_RxEventCallback(&uart,position); position=0; dma.remaining=UART_DMA_RX_CAPACITY;
        }
    }
    if (idle) ELRS_RxEventCallback(&uart,position);
}
static std::array<uint8_t,26> frame(const std::array<uint16_t,16> &channels)
{
    std::array<uint8_t,26> f{}; f[0]=0xC8; f[1]=24; f[2]=0x16;
    for (size_t c=0;c<16;++c)
        for (size_t b=0;b<11;++b)
            if ((channels[c]>>b)&1) f[3+(c*11+b)/8]|=static_cast<uint8_t>(1U<<((c*11+b)%8));
    uint8_t crc=0;
    for (size_t i=2;i<25;++i)
    {
        crc^=f[i];
        for (unsigned int b=0;b<8;++b) crc=(crc&0x80)?static_cast<uint8_t>((crc<<1)^0xD5):static_cast<uint8_t>(crc<<1);
    }
    f[25]=crc; return f;
}
static void send(const std::array<uint8_t,26> &f) { receive(f.data(),f.size()); ELRS_Poll(); }
int main()
{
    dma.Instance=&stream;
    ELRS_Init(&uart);
    std::array<uint16_t,16> channels; channels.fill(992); channels[0]=800; channels[1]=1200; channels[4]=1650;
    auto f=frame(channels);
    receive(f.data(),f.size(),false);
    ELRS_RxTick();
    CHECK(ELRS_GetChannel(0)==992); /* ISR did not unpack channels. */
    ELRS_Poll(); CHECK(ELRS_GetChannel(0)==800 && !ELRS_IsArmed());
    tick+=10; send(f); tick+=10; send(f);
    CHECK(ELRS_IsArmed() && ELRS_IsLinkActive(tick) && drives==0 && stops==0);
    ELRS_Process(tick); CHECK(drives==1);
    /* No intermediate poll during loss: the first new frame must not retain
     * the old armed state or bypass the three-frame filter. */
    tick+=301; CHECK(!ELRS_IsArmed()); send(f);
    CHECK(!ELRS_IsArmed() && ELRS_IsLinkActive(tick));
    tick+=10; send(f); CHECK(!ELRS_IsArmed());
    tick+=10; send(f); CHECK(ELRS_IsArmed());
    const uint8_t truncated[]={0xC8,62,0x16};
    receive(truncated,sizeof(truncated)); ELRS_Poll();
    channels[0]=900; f=frame(channels); tick+=11; send(f);
    CHECK(ELRS_GetChannel(0)==900 && ELRS_IsArmed());
    for (unsigned int i=0;i<150;++i) { ++tick; send(f); }
    CHECK(ELRS_IsArmed());
    auto bad=f; bad[25]^=1; tick+=301; send(bad);
    CHECK(!ELRS_IsLinkActive(tick)); ELRS_Process(tick); CHECK(stops==1);
    for (unsigned int i=0;i<3;++i) send(f);
    CHECK(ELRS_IsArmed());
    auto old_aborts=aborts;
    uart.ErrorCode=HAL_UART_ERROR_ORE; ELRS_ErrorCallback(&uart);
    CHECK(!ELRS_IsArmed() && aborts==old_aborts);
    tick+=10; ELRS_Poll(); CHECK(aborts==old_aborts+1);
    CHECK(uart.RxState==HAL_UART_STATE_BUSY_RX && !ELRS_IsArmed());
    for (unsigned int i=0;i<3;++i) send(f);
    CHECK(ELRS_IsArmed());
    tick+=301; ELRS_Poll();
    receive(f.data(),f.size()); tick+=301; ELRS_Poll(); CHECK(!ELRS_IsLinkActive(tick));
    budget=true; receive(f.data(),f.size()); ELRS_Poll(); CHECK(!ELRS_IsLinkActive(tick));
    budget=false; ELRS_Poll(); CHECK(ELRS_IsLinkActive(tick));
    std::array<uint8_t,1024> flood{}; receive(flood.data(),flood.size()); ELRS_Poll();
    CHECK(!ELRS_IsArmed());
    next_start=HAL_ERROR; ELRS_ErrorCallback(&uart); tick+=10; ELRS_Poll();
    CHECK(uart.RxState==HAL_UART_STATE_READY); tick+=10; ELRS_Poll();
    CHECK(uart.RxState==HAL_UART_STATE_BUSY_RX);
    for (unsigned int i=0;i<3;++i) send(f);
    CHECK(ELRS_IsArmed());
    ELRS_Statistics_t stats{}; ELRS_GetStatistics(&stats);
    CHECK(stats.uart_errors==2 && stats.rx_overruns>=1 && stats.rx_restart_errors==1 && mask==0);
    /* Simulate a previous abort timeout: HAL UART is READY, DMA is TIMEOUT. */
    dma.State=HAL_DMA_STATE_TIMEOUT; uart.RxState=HAL_UART_STATE_READY;
    tick+=10; ELRS_Poll(); CHECK(dma_reinitializations==1 && dma.State==HAL_DMA_STATE_READY);
    CHECK(uart.RxState==HAL_UART_STATE_BUSY_RX);
    abort_rx_fails=true; stream.CR=DMA_SxCR_EN;
    ELRS_ErrorCallback(&uart); tick+=10; ELRS_Poll(); CHECK(!ELRS_IsArmed());
    abort_rx_fails=false; tick+=10; ELRS_Poll();
    CHECK(stream.CR!=0 && !ELRS_IsLinkActive(tick)); /* Do not rearm a live stream. */
    stream.CR=0; tick+=10; ELRS_Poll();
    CHECK(dma_reinitializations==2 && uart.RxState==HAL_UART_STATE_BUSY_RX);
    std::puts("PASS ELRS DMA, bounded parsing, link-gap rearming, partial-frame timeout, ORE and overflow");
}
