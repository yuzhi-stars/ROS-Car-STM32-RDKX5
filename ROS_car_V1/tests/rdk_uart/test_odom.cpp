#include "Odom.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"ODOM FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
static uint32_t mask;
static std::array<int32_t,4> counts{};
static bool require_locked;
static unsigned int lock_count;
extern "C" {
uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask=1; ++lock_count; }
void __set_PRIMASK(uint32_t m) { mask=m; }
void Motor_GetAllEncoderCounts(int32_t out[4])
{
    if (require_locked) CHECK(mask==1);
    std::memcpy(out,counts.data(),sizeof(int32_t)*4);
}
float Motor_GetEncoderSamplePeriodS(void) { return .025f; }
}
int main()
{
    require_locked=true; Odom_Init(); CHECK(mask==0);
    counts.fill(100); require_locked=false; Odom_Update20ms();
    Odom_State_t state{}; auto locks=lock_count; Odom_GetState(&state);
    CHECK(mask==0 && lock_count==locks+1);
    CHECK(std::abs(state.x_m-100*.251327412f/1333)<1e-7 && state.y_m==0);
    CHECK(std::abs(state.vx_mps-state.x_m/.025f)<1e-6);
    float x,y,yaw,vx,vy,wz; Odom_GetPose(&x,&y,&yaw); Odom_GetVelocity(&vx,&vy,&wz);
    CHECK(x==state.x_m && y==state.y_m && yaw==state.yaw_rad);
    CHECK(vx==state.vx_mps && vy==0 && wz==0);

    require_locked=true; counts.fill(200); Odom_SetPose(1,2,1e30f); CHECK(mask==0);
    Odom_GetState(&state); CHECK(state.x_m==1 && state.y_m==2 && std::abs(state.yaw_rad)<=3.141592654f);
    auto saved=state; Odom_SetPose(3,4,std::numeric_limits<float>::infinity());
    Odom_SetPose(std::numeric_limits<float>::quiet_NaN(),0,0); Odom_GetState(&state);
    CHECK(std::memcmp(&state,&saved,sizeof(state))==0);
    require_locked=false; Odom_Update20ms(); Odom_GetState(&state);
    CHECK(state.x_m==1 && state.y_m==2 && state.vx_mps==0); /* Baseline rebased atomically. */

    mask=1; require_locked=true; Odom_Reset(); CHECK(mask==1);
    Odom_SetPose(4,5,-7); CHECK(mask==1); Odom_GetState(&state); CHECK(mask==1);
    CHECK(state.x_m==4 && state.y_m==5 && std::abs(state.yaw_rad)<3.141592654f);
    Odom_GetPose(nullptr,nullptr,nullptr); Odom_GetVelocity(nullptr,nullptr,nullptr); Odom_GetState(nullptr);
    mask=0; Odom_Reset(); Odom_GetState(&state);
    CHECK(state.x_m==0 && state.y_m==0 && state.yaw_rad==0 && mask==0);
    std::puts("PASS odometry coherent reads, actual sample period, atomic rebasing, bounded yaw and nested interrupt mask");
}
