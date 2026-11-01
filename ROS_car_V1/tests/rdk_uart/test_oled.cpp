#include "OLED.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"OLED FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
static uint32_t tick, transfers, startup_calls, resets;
static uint8_t *tx;
static uint16_t length;
static bool hardware_active, panel_on, lose_abort;
static uint8_t page, column;
static std::array<std::array<uint8_t,128>,8> panel{};
extern "C" {
I2C_HandleTypeDef hi2c1{};
uint32_t HAL_GetTick(void) { return tick; }
void HAL_Delay(uint32_t ms) { tick+=ms; }
uint32_t HAL_I2C_GetState(I2C_HandleTypeDef *i2c) { return i2c->State; }
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *,uint16_t,uint8_t *bytes,uint16_t size,uint32_t)
{
    ++startup_calls; CHECK(bytes[0]==0 && std::find(bytes,bytes+size,0xAF)==bytes+size);
    panel_on=false; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_DMA(I2C_HandleTypeDef *i2c,uint16_t,uint8_t *bytes,uint16_t size)
{
    CHECK(!hardware_active && i2c->State==HAL_I2C_STATE_READY && !i2c->busy);
    CHECK(size<=33); tx=bytes; length=size; hardware_active=true; ++transfers;
    i2c->State=HAL_I2C_STATE_BUSY_TX; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *i2c,uint16_t)
{
    if (lose_abort) return HAL_BUSY;
    hardware_active=false; i2c->State=HAL_I2C_STATE_READY; OLED_ErrorCallback(i2c); return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c)
{
    hardware_active=false; i2c->State=HAL_I2C_STATE_READY; ++resets; return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c) { i2c->State=HAL_I2C_STATE_READY; return HAL_OK; }
}
static void complete()
{
    CHECK(hardware_active);
    if (tx[0]==0)
    {
        for (uint16_t i=1;i<length;++i)
        {
            auto c=tx[i];
            if (c==0xAE) panel_on=false;
            else if (c==0xAF) panel_on=true;
            else if ((c&0xF8)==0xB0) page=c&7;
            else if ((c&0xF0)==0) column=static_cast<uint8_t>((column&0xF0)|(c&15));
            else if ((c&0xF0)==0x10) column=static_cast<uint8_t>((column&15)|((c&15)<<4));
        }
    }
    else
    {
        CHECK(tx[0]==0x40 && length<=33);
        for (uint16_t i=1;i<length;++i) { CHECK(column<128); panel[page][column++]=tx[i]; }
    }
    hardware_active=false; hi2c1.State=HAL_I2C_STATE_READY; OLED_TxCpltCallback(&hi2c1);
}
static void pump(unsigned int count=600)
{
    for (unsigned int i=0;i<count;++i) { OLED_Process(tick); if (hardware_active) complete(); ++tick; }
}
int main()
{
    for (auto &row:panel) row.fill(0xA5);
    OLED_Init(); OLED_ShowString(0,0,"CAL IMU"); pump(500);
    CHECK(!panel_on && transfers==0 && startup_calls==1);
    OLED_Clear(); OLED_ShowStringInverted(0,0,"A "); OLED_DisplayOn(); pump();
    CHECK(panel_on && panel[0][5]==0xFF && panel[0][6]==0xFF);
    for (auto b:panel[7]) CHECK(b==0); /* Unknown RAM including FF must be uploaded. */
    auto previous=transfers; OLED_ShowStringInverted(0,0,"A "); pump(100);
    CHECK(transfers==previous);
    OLED_ClearLine(0); OLED_ShowString(0,0,"A");
    OLED_Process(tick); CHECK(hardware_active && tx[0]==0); complete();
    OLED_Process(++tick); CHECK(OLED_I2CAvailable());
    OLED_Process(++tick); CHECK(hardware_active && tx[0]==0x40);
    auto snapshot=std::vector<uint8_t>(tx,tx+length);
    OLED_ShowString(0,0,"B"); CHECK(std::vector<uint8_t>(tx,tx+length)==snapshot);
    complete(); pump(); CHECK(panel[0][0]==0x7F && panel[0][5]==0);
    OLED_DisplayOff(); pump(); CHECK(!panel_on);
    previous=transfers; OLED_ShowString(2,0,"HIDDEN"); pump(100); CHECK(transfers==previous);
    OLED_DisplayOn(); pump(); CHECK(panel_on);
    hi2c1.busy=1; OLED_Clear(); previous=transfers; pump(20); CHECK(transfers==previous && !OLED_I2CAvailable());
    hi2c1.busy=0; pump();
    OLED_ShowString(0,0,"ERROR"); OLED_Process(tick); CHECK(hardware_active);
    hardware_active=false; hi2c1.State=HAL_I2C_STATE_READY; OLED_ErrorCallback(&hi2c1);
    OLED_Process(++tick); previous=transfers; pump(100); CHECK(transfers==previous);
    tick+=1000; pump(); CHECK(panel_on);
    OLED_ShowString(0,0,"TIMEOUT"); OLED_Process(tick); CHECK(hardware_active);
    lose_abort=true; tick+=11; OLED_Process(tick); CHECK(resets==1);
    lose_abort=false; tick+=1000; pump(); CHECK(panel_on);
    OLED_ShowString(0,0,"OFF ERROR"); OLED_Process(tick); CHECK(hardware_active);
    hardware_active=false; hi2c1.State=HAL_I2C_STATE_READY; OLED_ErrorCallback(&hi2c1);
    OLED_Process(++tick); OLED_DisplayOff(); CHECK(panel_on);
    previous=transfers; pump(100); CHECK(transfers==previous && panel_on);
    tick+=1000; pump(); CHECK(!panel_on);
    previous=transfers; pump(100); CHECK(transfers==previous);
    OLED_DisplayOn(); pump(); CHECK(panel_on);
    OLED_Statistics_t stats{}; OLED_GetStatistics(&stats);
    CHECK(stats.errors==3 && stats.recoveries==1 && stats.chunks>32);
    std::puts("PASS OLED boot-off, press-on upload, dirty regions, inverted rows, DMA lifetime, bus yielding and fault recovery");
}
