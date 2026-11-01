#include "imu_attitude.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "ATTITUDE FAIL line %d: %s\n", __LINE__, #condition); \
    std::exit(1); } } while (0)

using Quaternion = std::array<double, 4>;
static constexpr double pi = 3.14159265358979323846;
static const float zero[] = {0, 0, 0};
static const float gravity[] = {0, 0, IMU_ATTITUDE_GRAVITY};

static Quaternion multiply(const Quaternion &q, const Quaternion &r)
{
    return {q[3]*r[0]+q[0]*r[3]+q[1]*r[2]-q[2]*r[1],
            q[3]*r[1]-q[0]*r[2]+q[1]*r[3]+q[2]*r[0],
            q[3]*r[2]+q[0]*r[1]-q[1]*r[0]+q[2]*r[3],
            q[3]*r[3]-q[0]*r[0]-q[1]*r[1]-q[2]*r[2]};
}

static Quaternion euler(double roll, double pitch, double yaw = 0)
{
    double sr=std::sin(roll/2), cr=std::cos(roll/2);
    double sp=std::sin(pitch/2), cp=std::cos(pitch/2);
    double sy=std::sin(yaw/2), cy=std::cos(yaw/2);
    return {sr*cp*cy-cr*sp*sy, cr*sp*cy+sr*cp*sy,
            cr*cp*sy-sr*sp*cy, cr*cp*cy+sr*sp*sy};
}

static std::array<float, 3> body_gravity(const Quaternion &q)
{
    return {static_cast<float>(2*(q[0]*q[2]-q[3]*q[1])*IMU_ATTITUDE_GRAVITY),
            static_cast<float>(2*(q[1]*q[2]+q[3]*q[0])*IMU_ATTITUDE_GRAVITY),
            static_cast<float>((1-2*(q[0]*q[0]+q[1]*q[1]))*IMU_ATTITUDE_GRAVITY)};
}

static double angle_error(const float q[4], const Quaternion &truth)
{
    double norm=0, truth_norm=0, dot=0;
    for (size_t i=0; i<4; ++i)
    {
        norm+=static_cast<double>(q[i])*q[i];
        truth_norm+=truth[i]*truth[i];
        dot+=q[i]*truth[i];
    }
    dot=std::abs(dot)/std::sqrt(norm*truth_norm);
    return 2*std::acos(dot > 1 ? 1 : dot);
}

static IMU_Attitude_t aligned(const Quaternion &q = {0,0,0,1})
{
    IMU_Attitude_t state;
    IMU_Attitude_Init(&state);
    auto acceleration=body_gravity(q);
    for (unsigned int i=0; i<IMU_ATTITUDE_INIT_SAMPLES; ++i)
    {
        bool valid=IMU_Attitude_Update(&state, zero, acceleration.data(), .01f);
        CHECK(valid == (i+1 == IMU_ATTITUDE_INIT_SAMPLES));
    }
    CHECK(state.initialized);
    CHECK(angle_error(state.xyzw, q) < .001);
    return state;
}

int main()
{
    auto level=aligned();
    (void)aligned(euler(30*pi/180, -20*pi/180));
    (void)aligned(euler(pi, 0));
    (void)aligned(euler(0, pi/2));
    IMU_Attitude_t state;
    IMU_Attitude_Init(&state);
    const float moving[] = {0,0,.2f};
    for (unsigned int i=0; i<100; ++i)
        CHECK(!IMU_Attitude_Update(&state, moving, gravity, .01f));
    CHECK(!state.initialized && !state.alignment_samples);
    for (unsigned int i=0; i<20; ++i) IMU_Attitude_Update(&state, zero, gravity, .01f);
    CHECK(!IMU_Attitude_Update(&state, zero, nullptr, .01f) && state.alignment_samples==20);
    CHECK(!IMU_Attitude_Update(&state, zero, nullptr, .01f) && state.alignment_samples==20);
    CHECK(!IMU_Attitude_Update(&state, zero, nullptr, .01f) && state.alignment_samples==20);
    CHECK(!IMU_Attitude_Update(&state, zero, gravity, .01f) && state.alignment_samples==1);
    /* Fresh data arrived after 40 ms: discard the old alignment window. */
    for (unsigned int i=0;i<4;++i) IMU_Attitude_Update(&state, zero, nullptr, .01f);
    CHECK(!state.alignment_samples);
    IMU_Attitude_Init(&state);
    unsigned int fresh_count=0;
    for (unsigned int i=0;i<100 && !state.initialized;++i)
    {
        bool fresh=i%25!=24;
        if (fresh) ++fresh_count;
        bool valid=IMU_Attitude_Update(&state, zero, fresh ? gravity : nullptr, .01f);
        CHECK(valid==(fresh_count==IMU_ATTITUDE_INIT_SAMPLES));
    }
    CHECK(state.initialized && fresh_count==IMU_ATTITUDE_INIT_SAMPLES);
    IMU_Attitude_Init(&state);
    for (unsigned int i=0;i<20;++i) IMU_Attitude_Update(&state, zero, gravity, .01f);
    CHECK(!IMU_Attitude_Update(&state, moving, nullptr, .01f) && !state.alignment_samples);
    std::puts("PASS intermittent fresh accel alignment, exact fresh-sample count, long outage and motion reset");
    std::puts("PASS stationary alignment, tilted/inverted startup, gimbal orientation and moving-start rejection");

    /* Independent double-precision truth integrates known body rates. The
     * accelerometer vector comes from truth, not from the filter under test. */
    const std::array<std::array<float,3>,5> rates{{
        {{static_cast<float>(pi/2),0,0}}, {{0,static_cast<float>(-pi/2),0}},
        {{0,0,static_cast<float>(pi/2)}}, {{.4f,-.6f,.8f}}, {{4.f,3.f,-2.f}}
    }};
    double worst_error=0;
    for (const auto &rate : rates)
    {
        state=aligned();
        Quaternion truth{0,0,0,1};
        for (unsigned int i=0; i<300; ++i)
        {
            float dt = i%17 == 0 ? .04f : (i%2 ? .015f : .005f);
            auto accel=body_gravity(truth);
            CHECK(IMU_Attitude_Update(&state, rate.data(), accel.data(), dt));
            double speed=std::sqrt(rate[0]*rate[0]+rate[1]*rate[1]+rate[2]*rate[2]);
            double scale=std::sin(speed*dt/2)/speed;
            truth=multiply(truth, {rate[0]*scale,rate[1]*scale,rate[2]*scale,std::cos(speed*dt/2)});
            double error=angle_error(state.xyzw, truth);
            if (error>worst_error) worst_error=error;
            CHECK(error < .001);
        }
    }
    std::printf("PASS roll/pitch/yaw and compound rotations with variable dt (max error %.5f deg)\n",
                worst_error*180/pi);

    state=aligned();
    auto wrong=euler(10*pi/180, 0);
    for (size_t i=0; i<4; ++i) state.xyzw[i]=static_cast<float>(wrong[i]);
    for (unsigned int i=0; i<3000; ++i) CHECK(IMU_Attitude_Update(&state, zero, gravity, .01f));
    CHECK(angle_error(state.xyzw, {0,0,0,1}) < .5*pi/180);
    state=aligned();
    const float residual_bias[] = {.01f,0,0};
    for (unsigned int i=0; i<10000; ++i)
        CHECK(IMU_Attitude_Update(&state, residual_bias, gravity, .01f));
    CHECK(angle_error(state.xyzw, {0,0,0,1}) < .2*pi/180);
    CHECK(state.integral[0] < -.008f);
    for (float integral : state.integral) CHECK(std::abs(integral) <= IMU_ATTITUDE_INTEGRAL_LIMIT_RAD_S);
    std::puts("PASS gravity feedback convergence and bounded integral correction of observable gyro bias");

    state=aligned();
    const float impulse[] = {IMU_ATTITUDE_GRAVITY, 0, 2*IMU_ATTITUDE_GRAVITY};
    const float false_tilt[] = {IMU_ATTITUDE_GRAVITY/std::sqrt(2.f),0,
                               IMU_ATTITUDE_GRAVITY/std::sqrt(2.f)};
    const float nan_accel[] = {std::numeric_limits<float>::quiet_NaN(),0,0};
    for (unsigned int i=0; i<100; ++i)
    {
        CHECK(IMU_Attitude_Update(&state, zero, impulse, .01f));
        CHECK(IMU_Attitude_Update(&state, zero, false_tilt, .01f));
        CHECK(IMU_Attitude_Update(&state, zero, zero, .01f));
        CHECK(IMU_Attitude_Update(&state, zero, nan_accel, .01f));
    }
    CHECK(angle_error(state.xyzw, {0,0,0,1}) < .001);
    const float yaw_bias[] = {0,0,.01f};
    for (unsigned int i=0; i<1000; ++i)
        CHECK(IMU_Attitude_Update(&state, yaw_bias, gravity, .01f));
    CHECK(angle_error(state.xyzw, euler(0,0,.1)) < .001);
    std::puts("PASS acceleration/innovation rejection, free-fall and six-axis yaw observability limitation");

    for (float bad_dt : {0.f, -.01f, .051f, std::numeric_limits<float>::quiet_NaN()})
    {
        state=level;
        CHECK(!IMU_Attitude_Update(&state, zero, gravity, bad_dt) && !state.initialized);
    }
    state=level;
    const float bad_gyro[] = {std::numeric_limits<float>::infinity(),0,0};
    CHECK(!IMU_Attitude_Update(&state, bad_gyro, gravity, .01f) && !state.initialized);
    CHECK(!IMU_Attitude_Update(nullptr, zero, gravity, .01f));
    IMU_Attitude_Init(nullptr);
    state=aligned();
    for (unsigned int i=0; i<10000; ++i)
        CHECK(IMU_Attitude_Update(&state, moving, nullptr, .01f));
    double norm=0;
    for (float component : state.xyzw) { CHECK(std::isfinite(component)); norm+=component*component; }
    CHECK(std::abs(norm-1) < 1e-5);
    std::puts("PASS invalid samples/dt, sampling-gap reset and long-run quaternion normalization");
    std::puts("ALL IMU ATTITUDE TESTS PASSED");
}
