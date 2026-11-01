#include "imu_calibration.h"
#include "motor_speed_filter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"CALIBRATION FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
static const float gyro[]={20,70,-30};
static const float accel[]={.008f,.030f,1.009f};
static IMU_Calibration_t cal;
static uint32_t now;
static bool update(uint8_t fresh=3,uint8_t allowed=1,const float *g=gyro,const float *a=accel)
{
    now+=10;
    return IMU_Calibration_Update(&cal,g,a,fresh,now,allowed);
}
static void stationary(unsigned int samples=300)
{
    for (unsigned int i=0;i<samples;++i) update();
}
int main()
{
    IMU_Calibration_Init(&cal);
    for (unsigned int i=0;i<300;++i)
    {
        /* Sensors can have separate DRDY phases: gyro every call, accel /2. */
        bool valid=update(i%2==0 ? 3 : 2);
        CHECK(valid==(i>=200));
    }
    CHECK(cal.valid && cal.samples==201 && cal.rejected_windows==0);
    for (unsigned int i=0;i<3;++i) CHECK(std::abs(cal.bias_raw[i]-gyro[i])<1e-5);
    auto old_bias=cal.bias_raw[0]; update(3,0); CHECK(cal.valid && cal.bias_raw[0]==old_bias);

    IMU_Calibration_Init(&cal);
    for (unsigned int i=0;i<500;++i) update(i%2==0 ? 2 : 1);
    CHECK(cal.valid); /* Never a simultaneous gyro+accel-ready call. */

    IMU_Calibration_Init(&cal);
    for (unsigned int i=0;i<400;++i) update(1);
    CHECK(!cal.valid && cal.samples==0); /* Accel-only calls add no gyro samples. */
    update(3); CHECK(cal.samples==1); update(1); CHECK(cal.samples==1);
    update(3,0); CHECK(!cal.samples && cal.last_reason==IMU_CAL_DRIVE);
    for (unsigned int i=0;i<300;++i) update(3,0);
    CHECK(!cal.valid); stationary(); CHECK(cal.valid);

    IMU_Calibration_Init(&cal);
    const float moving[]={1000,0,0};
    for (unsigned int i=0;i<100;++i) update(3,1,moving);
    CHECK(!cal.valid && cal.last_reason==IMU_CAL_GYRO);
    stationary(); CHECK(cal.valid);

    IMU_Calibration_Init(&cal);
    for (unsigned int i=0;i<201;++i)
    {
        float noisy[]={i%2==0 ? 100.f : -100.f,0,0};
        update(3,1,noisy);
    }
    CHECK(!cal.valid && cal.last_reason==IMU_CAL_NOISE && cal.rejected_windows==1);
    stationary(); CHECK(cal.valid);

    IMU_Calibration_Init(&cal); stationary(50);
    for (unsigned int i=0;i<4;++i) update(2);
    CHECK(!cal.valid && !cal.samples && cal.last_reason==IMU_CAL_ACCEL);
    stationary(); CHECK(cal.valid);
    IMU_Calibration_Init(&cal); stationary(50);
    now+=60; update(); CHECK(!cal.samples && cal.last_reason==IMU_CAL_GAP);
    stationary(); CHECK(cal.valid);

    IMU_Calibration_Init(&cal); stationary(50);
    const float bad[]={std::numeric_limits<float>::quiet_NaN(),0,0};
    update(3,1,bad); CHECK(!cal.samples && cal.last_reason==IMU_CAL_GYRO);
    update(3,1,gyro,bad); CHECK(!cal.samples && cal.last_reason==IMU_CAL_GRAVITY);
    const float freefall[]={0,0,0};
    update(3,1,gyro,freefall); CHECK(!cal.samples && cal.last_reason==IMU_CAL_GRAVITY);
    stationary(); CHECK(cal.valid);
    now=UINT32_MAX-1000U; IMU_Calibration_Init(&cal); stationary(); CHECK(cal.valid);
    std::puts("PASS gyro calibration: independent fresh groups, startup retry, drive pause, variance, stale/gap rejection and clock wrap");

    for (float initial : {400.f,-400.f,2.f,-2.f})
    {
        float filtered=initial;
        for (unsigned int i=0;i<100;++i) filtered=MotorSpeedFilter_Update(filtered,0);
        CHECK(filtered==0 && std::lround(filtered)==0);
    }
    float filtered=0;
    for (unsigned int i=1;i<=20;++i)
    {
        filtered=MotorSpeedFilter_Update(filtered,400);
        double expected=400*(1-std::pow(.65,static_cast<double>(i)));
        CHECK(std::abs(filtered-expected)<.0001);
    }
    filtered=0;
    for (unsigned int i=0;i<100;++i) filtered=MotorSpeedFilter_Update(filtered,.25f);
    CHECK(std::abs(filtered-.25f)<1e-6); /* Fractional state is retained. */
    std::puts("PASS wheel filter: +/-zero tail eliminated, alpha=0.35 response and fractional low-speed state");
}
