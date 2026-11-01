#include "drive_control.h"
#include "ELRS.h"
#include "rdk_uart.h"
#include "uart_dma_rx.h"
#include "Mecanum.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CONTROL FAIL %d at %ums: %s\n",__LINE__,tick,#c); std::exit(1); } } while(0)
static uint32_t tick,mask,stops,transmissions;
static std::array<int32_t,4> targets{};
static DMA_Stream_TypeDef host_rx_stream{},radio_rx_stream{},tx_stream{};
static DMA_HandleTypeDef host_rx{0,&host_rx_stream,0},radio_rx{0,&radio_rx_stream,0},host_tx{0,&tx_stream,0};
static UART_HandleTypeDef host_uart{0,0,0,&host_rx,&host_tx},radio_uart{0,0,0,&radio_rx,nullptr};
struct ReceiveState { uint8_t *bytes{}; uint16_t position{}; };
static ReceiveState host_buffer,radio_buffer;
static uint8_t *tx_data;
static uint16_t tx_size;
static std::array<uint16_t,16> channels;

extern "C" {
uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t value) { mask=value; }
uint32_t RuntimeClock_Cycles(void) { return 0; }
uint8_t RuntimeClock_BudgetExpired(uint32_t,uint32_t) { return 0; }
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u,uint8_t *data,uint16_t size)
{
    CHECK(size==UART_DMA_RX_CAPACITY);
    auto &rx=u==&host_uart ? host_buffer : radio_buffer;
    rx.bytes=data; rx.position=0; u->hdmarx->remaining=size;
    u->RxState=HAL_UART_STATE_BUSY_RX; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{
    CHECK(mask==0); u->RxState=HAL_UART_STATE_READY;
    u->hdmarx->State=HAL_DMA_STATE_READY; u->hdmarx->Instance->CR=0; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *u,uint8_t *data,uint16_t size)
{
    CHECK(u==&host_uart && u->gState==HAL_UART_STATE_READY && tx_stream.CR==0);
    CHECK(size==68); tx_data=data; tx_size=size; ++transmissions;
    u->gState=HAL_UART_STATE_BUSY_TX; tx_stream.CR=DMA_SxCR_EN; return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{
    CHECK(mask==0); u->gState=HAL_UART_STATE_READY; tx_stream.CR=0; return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *d)
{
    CHECK(mask==0); d->State=HAL_DMA_STATE_READY; d->Instance->CR=0; return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *d) { CHECK(mask==0); d->Instance->CR=0; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d) { CHECK(mask==0); d->State=HAL_DMA_STATE_READY; return HAL_OK; }
void Motor_ClosedLoopEmergencyStop(void) { CHECK(mask==1); targets.fill(0); ++stops; }
void Motor_ClosedLoopSetTarget(Motor_ID_t motor,int32_t value) { CHECK(mask==1); targets.at(motor)=value; }
void Motor_ClosedLoopSetAllTarget(int32_t value) { targets.fill(value); }
void Motor_GetAllEncoderSpeeds(int32_t out[4]) { CHECK(mask==1); for (unsigned int i=0;i<4;++i) out[i]=0; }
uint32_t Motor_GetEncoderSampleTick(void) { return tick; }
}
static void advance(uint32_t ms)
{
    for (uint32_t i=0;i<ms;++i)
    {
        ++tick; DriveControl_WatchdogTick(tick); RDK_UART_RxTick(); ELRS_RxTick();
    }
}
static void receive(UART_HandleTypeDef *u,const uint8_t *data,size_t count,bool idle=true)
{
    auto &rx=u==&host_uart ? host_buffer : radio_buffer;
    auto event=[&]() {
        if (u==&host_uart) RDK_UART_RxEventCallback(u,rx.position);
        else ELRS_RxEventCallback(u,rx.position);
    };
    for (size_t i=0;i<count;++i)
    {
        CHECK(u->RxState==HAL_UART_STATE_BUSY_RX);
        rx.bytes[rx.position++]=data[i]; u->hdmarx->remaining=UART_DMA_RX_CAPACITY-rx.position;
        if (rx.position==UART_DMA_RX_CAPACITY/2) event();
        if (rx.position==UART_DMA_RX_CAPACITY) { event(); rx.position=0; u->hdmarx->remaining=UART_DMA_RX_CAPACITY; }
    }
    if (idle) event();
}
/* Local wire fixture keeps ownership tests runnable without desktop files.
 * Original host-code interoperability remains in test_rdk_uart.cpp. */
static uint16_t wire_crc(const uint8_t *data, size_t count)
{
    uint16_t crc=0xFFFF;
    for (size_t i=0;i<count;++i)
    {
        crc^=static_cast<uint16_t>(data[i])<<8;
        for (unsigned int b=0;b<8;++b)
            crc=(crc&0x8000) ? static_cast<uint16_t>((crc<<1)^0x1021) : static_cast<uint16_t>(crc<<1);
    }
    return crc;
}
static auto host_frame(std::array<float,4> wheels)
{
    std::array<uint8_t,21> frame{};
    frame[0]=0xAA; frame[1]=0x55; frame[2]=0x03;
    std::memcpy(frame.data()+3,wheels.data(),16);
    auto crc=wire_crc(frame.data()+2,17);
    frame[19]=static_cast<uint8_t>(crc); frame[20]=static_cast<uint8_t>(crc>>8);
    return frame;
}
static void send_host(std::array<float,4> wheels)
{
    auto f=host_frame(wheels); receive(&host_uart,f.data(),f.size()); RDK_UART_PollRx();
}
static void send_radio(bool poll_host=true)
{
    advance(10);
    std::array<uint8_t,26> f{}; f[0]=0xC8; f[1]=24; f[2]=0x16;
    for (size_t c=0;c<16;++c)
        for (size_t b=0;b<11;++b)
            if ((channels[c]>>b)&1) f[3+(c*11+b)/8]|=static_cast<uint8_t>(1U<<((c*11+b)%8));
    uint8_t crc=0;
    for (size_t i=2;i<25;++i)
    {
        crc^=f[i];
        for (unsigned int b=0;b<8;++b) crc=(crc&0x80) ? static_cast<uint8_t>((crc<<1)^0xD5) : static_cast<uint8_t>(crc<<1);
    }
    f[25]=crc; receive(&radio_uart,f.data(),f.size()); ELRS_Poll(); DriveControl_Process();
    if (poll_host) RDK_UART_PollRx();
}
static void radio_frames(unsigned int count=8,bool poll_host=true) { for (unsigned int i=0;i<count;++i) send_radio(poll_host); }
static void stopped()
{
    CHECK(!DriveControl_ControlTick(tick));
    for (auto target:targets) CHECK(target==0);
}
static void finish_tx()
{
    host_uart.gState=HAL_UART_STATE_READY; tx_stream.CR=0; RDK_UART_TxCpltCallback(&host_uart);
}
int main(int argc,char **argv)
{
    CHECK(RDK_UART_Init(&host_uart)==HAL_OK); ELRS_Init(&radio_uart);
    if (argc>1)
    {
        DriveControl_Init(); RDK_UART_PollRx(); stopped();
        const bool manual=std::string(argv[1])=="boot-remote";
        const bool arm_low=std::string(argv[1])=="boot-radio-low";
        channels.fill(992); channels[4]=arm_low ? 172 : 1811; channels[5]=manual ? 1811 : 172;
        radio_frames();
        if (manual || arm_low)
        {
            stopped(); CHECK(!RDK_UART_IsControlEnabled());
            send_host({2,2,2,2}); stopped();
            channels[4]=172; radio_frames(3); channels[4]=1811; radio_frames();
        }
        if (manual)
        {
            CHECK(DriveControl_ControlTick(tick) && DriveControl_GetMode()==DRIVE_REMOTE);
            std::puts("PASS fresh boot in REMOTE with arm high stays locked until rearm and centering");
        }
        else
        {
            CHECK(RDK_UART_IsControlEnabled() && !RDK_UART_IsCommandReady());
            CHECK(std::string(DriveControl_GetLabel())=="CTRL:HOST WAIT");
            send_host({2,2,2,2}); CHECK(DriveControl_ControlTick(tick) && targets[0]==424);
            std::puts("PASS fresh boot/reset with live radio in HOST accepts nonzero without zero handshake");
            advance(300); stopped(); radio_frames();
            send_host({2,2,2,2}); stopped(); CHECK(!RDK_UART_IsControlEnabled());
            channels[4]=172; radio_frames(3); channels[4]=1811; radio_frames();
            send_host({2,2,2,2}); CHECK(DriveControl_ControlTick(tick));
            std::puts("PASS HOST radio loss remains latched until CH5 rearm, then resumes with a fresh nonzero frame");
        }
        CHECK(mask==0); return 0;
    }

    int32_t calculated[4];
    Mecanum_CalcMotorTargets(0,100,0,1000,calculated);
    CHECK(calculated[0]==100 && calculated[1]==-100 && calculated[2]==-100 && calculated[3]==100);
    Mecanum_CalcMotorTargets(0,0,100,1000,calculated);
    CHECK(calculated[0]==-100 && calculated[1]==100 && calculated[2]==-100 && calculated[3]==100);
    /* The legacy RDK API keeps its strict policy for callers outside arbiter. */
    send_host({1,1,1,1}); CHECK(!RDK_UART_IsCommandReady());
    send_host({0,0,0,0}); CHECK(RDK_UART_IsCommandReady());
    DriveControl_Init();
    RDK_UART_PollRx(); stopped();
    send_host({2,2,2,2});
    CHECK(DriveControl_ControlTick(tick) && targets[0]==424 && DriveControl_GetMode()==DRIVE_HOST);
    std::puts("PASS standalone HOST accepts fresh nonzero, legacy strict RDK grant still requires zero");

    channels.fill(992); channels[4]=1811; channels[5]=172;
    radio_frames(); stopped(); CHECK(RDK_UART_IsControlEnabled());
    send_host({2,2,2,2}); CHECK(DriveControl_ControlTick(tick));
    /* Explicit HOST selection accepts arm high; REMOTE still needs rearm. */
    channels[4]=172; radio_frames(3); channels[5]=1811;
    radio_frames(3); CHECK(DriveControl_GetMode()==DRIVE_REMOTE); stopped();
    channels[4]=1811; radio_frames(); CHECK(std::string(DriveControl_GetLabel())=="CTRL:REMOTE" && DriveControl_IsEnabled());
    channels[1]=1811; send_radio(); CHECK(targets[0]==0); /* No foreground motor writes. */
    CHECK(DriveControl_ControlTick(tick)); for (auto target:targets) CHECK(target==1000);
    send_host({0,0,0,0}); send_host({4,4,4,4}); CHECK(!RDK_UART_IsCommandReady());
    CHECK(DriveControl_ControlTick(tick)); for (auto target:targets) CHECK(target==1000);
    auto before_errors=stops; host_uart.ErrorCode=HAL_UART_ERROR_ORE; RDK_UART_ErrorCallback(&host_uart);
    CHECK(stops==before_errors); advance(10); RDK_UART_PollRx();
    CHECK(DriveControl_ControlTick(tick)); for (auto target:targets) CHECK(target==1000);
    auto sent=transmissions; RDK_UART_Process(tick); CHECK(transmissions==sent+1);
    CHECK(tx_size==68 && tx_data[0]==0xAA && tx_data[1]==0x55 && tx_data[2]==0x90);
    CHECK(tx_data[22]==0xAA && tx_data[23]==0x55 && tx_data[24]==0x91);
    CHECK(wire_crc(tx_data+2,18)==static_cast<uint16_t>(tx_data[20]|(tx_data[21]<<8)));
    CHECK(wire_crc(tx_data+24,42)==static_cast<uint16_t>(tx_data[66]|(tx_data[67]<<8)));
    finish_tx();
    std::puts("PASS manual ownership, 1000 cps cap, ignored host commands/errors and continued feedback");

    ELRS_Snapshot_t snapshot{}; CHECK(ELRS_GetSnapshot(&snapshot));
    advance(299-(tick-snapshot.received_ms)); CHECK(DriveControl_ControlTick(tick));
    advance(1); CHECK(DriveControl_GetMode()==DRIVE_STOP); stopped();
    send_host({0,0,0,0}); send_host({2,2,2,2}); stopped();
    radio_frames(); stopped(); /* Reconnect with arm high never resumes motion. */
    channels[4]=172; radio_frames(3); channels[4]=1811; radio_frames(); stopped(); /* Stick deflected. */
    channels[1]=992; radio_frames(3); CHECK(DriveControl_ControlTick(tick));
    channels[1]=1811; send_radio(); CHECK(DriveControl_ControlTick(tick) && targets[0]==1000);
    std::puts("PASS 300 ms IRQ watchdog without foreground, latched radio loss, rearm and stick centering");

    /* Queued host zero+speed include bytes not yet reported by IDLE. */
    auto zero=host_frame({0,0,0,0}), old=host_frame({3,3,3,3});
    receive(&host_uart,old.data(),10); RDK_UART_PollRx(); /* Partial parser from inactive owner. */
    receive(&host_uart,old.data()+10,old.size()-10,false);
    receive(&host_uart,zero.data(),zero.size(),false); receive(&host_uart,old.data(),old.size(),false);
    channels[5]=172; send_radio(false); stopped(); /* First different position cuts PWM. */
    CHECK(DriveControl_GetMode()==DRIVE_STOP);
    radio_frames(8,false); CHECK(RDK_UART_IsControlEnabled());
    CHECK(DriveControl_GetMode()==DRIVE_HOST);
    RDK_UART_PollRx(); CHECK(!RDK_UART_IsCommandReady()); stopped();
    send_host({2,-2,1,-1});
    CHECK(DriveControl_ControlTick(tick)); CHECK((targets==std::array<int32_t,4>{424,-424,212,-212}));
    for (unsigned int i=0;i<4;++i) { advance(40); send_radio(); }
    CHECK(!RDK_UART_IsCommandReady()); stopped();
    CHECK(std::string(DriveControl_GetLabel())=="CTRL:HOST WAIT");
    send_host({1,1,1,1}); CHECK(DriveControl_ControlTick(tick));
    /* A radio poll must not regrant/flush an already live host command. */
    radio_frames(3); CHECK(DriveControl_ControlTick(tick) && RDK_UART_IsCommandReady());
    host_uart.ErrorCode=HAL_UART_ERROR_ORE; RDK_UART_ErrorCallback(&host_uart);
    stopped(); advance(10); RDK_UART_PollRx();
    send_host({1,1,1,1}); CHECK(DriveControl_ControlTick(tick));
    /* Fresh-command permission never bypasses CRC, finite/range or age checks. */
    RDK_UART_Statistics_t before{}; RDK_UART_GetStatistics(&before);
    auto bad=host_frame({2,2,2,2}); bad[19]^=1;
    receive(&host_uart,bad.data(),bad.size()); RDK_UART_PollRx();
    send_host({NAN,0,0,0}); send_host({100,0,0,0});
    RDK_UART_Statistics_t after{}; RDK_UART_GetStatistics(&after);
    CHECK(after.crc_errors>before.crc_errors && after.invalid_commands>=before.invalid_commands+2);
    CHECK(after.command_frames==before.command_frames);
    auto stale=host_frame({3,3,3,3}); receive(&host_uart,stale.data(),stale.size());
    radio_frames(20,false); stopped(); RDK_UART_PollRx(); stopped();
    RDK_UART_GetStatistics(&after); CHECK(after.stale_commands>before.stale_commands);
    send_host({1,1,1,1}); CHECK(DriveControl_ControlTick(tick));
    channels[4]=172; send_radio(); CHECK(!RDK_UART_IsControlEnabled()); stopped();
    send_host({2,2,2,2}); stopped();
    channels[4]=1811; radio_frames(); stopped(); /* New grant waits for new data. */
    send_host({1,1,1,1}); CHECK(DriveControl_ControlTick(tick));
    std::puts("PASS manual-to-HOST with CH5 high, RX barrier, nonzero takeover/timeout/error recovery and master disarm");
    std::puts("PASS rejected CRC/NaN/range/stale commands and repeated grants preserving live targets");

    channels[5]=992; channels[4]=1811; radio_frames(); CHECK(DriveControl_GetMode()==DRIVE_STOP); stopped();
    channels[5]=1811; channels[4]=172; channels[1]=992; radio_frames(3);
    channels[4]=1811; radio_frames(); CHECK(DriveControl_ControlTick(tick));
    channels[0]=172; channels[1]=1811; channels[3]=172; send_radio(); CHECK(DriveControl_ControlTick(tick));
    for (auto target:targets) CHECK(std::abs(target)<=DRIVE_REMOTE_WHEEL_LIMIT_CPS);
    radio_uart.ErrorCode=HAL_UART_ERROR_ORE; ELRS_ErrorCallback(&radio_uart);
    advance(1); CHECK(DriveControl_GetMode()==DRIVE_STOP); stopped();
    CHECK(!RDK_UART_IsControlEnabled() && mask==0);
    RDK_UART_Statistics_t statistics{}; RDK_UART_GetStatistics(&statistics);
    CHECK(statistics.commands_while_disabled>=2);
    std::puts("PASS CH6 middle stop, mixed manual wheel limiting and radio RX error cut-off");
}
