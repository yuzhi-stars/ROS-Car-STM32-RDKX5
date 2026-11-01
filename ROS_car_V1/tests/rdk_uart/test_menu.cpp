#include "Menu.h"
#include "OLED.h"
#include "rdk_uart.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"MENU FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
static uint32_t tick,mask,diagnostic_reads;
static uint8_t ab;
static bool pressed,display_on;
static std::array<std::string,8> rows{};
static unsigned int highlighted;
static RDK_UART_ImuDiagnostics_t diagnostics{};
extern "C" {
volatile uint8_t g_peripherals_initialized=1;
uint32_t HAL_GetTick(void) { return tick; }
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; }
void __set_PRIMASK(uint32_t m) { mask=m; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t pin)
{
    bool high=pin==KEY3_Pin ? !pressed : (pin==KEY1_Pin ? ((ab&2)!=0) : ((ab&1)!=0));
    return high ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_EXTI_Callback(uint16_t);
void OLED_Clear(void) { for (auto &row:rows) row.clear(); }
void OLED_ClearLine(uint8_t page) { CHECK(page<8); rows[page].clear(); }
void OLED_DisplayOn(void) { display_on=true; }
void OLED_DisplayOff(void) { display_on=false; }
void OLED_ShowString(uint8_t page,uint8_t column,const char *text)
{
    CHECK(page<8 && column==0); rows[page]=std::string(text).substr(0,21);
}
void OLED_ShowStringInverted(uint8_t page,uint8_t column,const char *text)
{
    OLED_ShowString(page,column,text); highlighted=page;
}
void Motor_ClosedLoopGetAllTargets(int32_t out[4]) { for (unsigned int i=0;i<4;++i) out[i]=0; }
void Motor_ClosedLoopSetTarget(Motor_ID_t,int32_t) { CHECK(false); }
void RDK_UART_GetImuDiagnostics(RDK_UART_ImuDiagnostics_t *d) { ++diagnostic_reads; *d=diagnostics; }
const char *DriveControl_GetLabel(void) { return "CTRL:HOST WAIT"; }
}
static void press()
{
    tick+=60; pressed=true; HAL_GPIO_EXTI_Callback(KEY3_Pin); Menu_Process(tick); pressed=false;
}
static void step()
{
    for (uint8_t next : {1,3,2,0})
    {
        tick+=3; ab=next; HAL_GPIO_EXTI_Callback(KEY1_Pin);
    }
    Menu_Process(tick);
}
int main()
{
    PID_Params_t pid[4]{};
    IMU_Calibration_t calibration{};
    Menu_Status_t status{}; status.imu_calibration=&calibration;
    Menu_Init(pid,&status); CHECK(!display_on);
    press(); CHECK(display_on && rows[6]=="IMU DIAG" && rows[7]=="DISPLAY OFF");
    for (unsigned int i=0;i<6;++i) step();
    CHECK(highlighted==6); press();
    CHECK(rows[0]=="IMU:WAIT CAL" && rows[1]=="CAL:WAIT STILL");
    calibration.last_reason=IMU_CAL_NOISE; calibration.samples=43; calibration.rejected_windows=2;
    tick+=100; Menu_Process(tick);
    CHECK(rows[2]=="WHY:GYRO NOISE" && rows[3]=="CAL N:43 R:2");
    diagnostics.bias_valid=1; diagnostics.alignment_samples=17;
    tick+=100; Menu_Process(tick); CHECK(rows[0]=="IMU:ALIGN" && rows[4]=="ALIGN:17/32 R:0");
    diagnostics.initialized=diagnostics.orientation_valid=1; diagnostics.alignment_samples=32;
    calibration.last_reason=IMU_CAL_NONE;
    tick+=100; Menu_Process(tick); CHECK(rows[0]=="IMU:READY" && rows[1]=="CAL:OK");
    auto reads=diagnostic_reads; tick+=99; Menu_Process(tick); CHECK(diagnostic_reads==reads);
    diagnostics.orientation_valid=0; diagnostics.gyro_age_ms=71; ++tick; Menu_Process(tick);
    CHECK(rows[0]=="IMU:STALE" && rows[5]=="AGE G:71 A:0 MS");
    press(); CHECK(rows[6]=="IMU DIAG"); step(); CHECK(highlighted==7);
    press(); CHECK(!display_on && mask==0);
    std::puts("PASS OLED IMU menu: boot off, encoder navigation, calibration reason/progress, alignment/ready/stale and 10 Hz refresh");
}
