#include "drive_control.h"
#include "ELRS.h"
#include "Mecanum.h"
#include "Motor.h"
#include "rdk_uart.h"

#define MODE_LOW_MAX 650U
#define MODE_HIGH_MIN 1350U
#define ARM_LOW_MAX 1400U
#define ARM_HIGH_MIN 1600U
#define STICK_MID 992
#define STICK_CENTER_TOLERANCE 20
#define STABLE_FRAMES 3U

typedef struct
{
    Drive_Mode_t mode, candidate;
    uint32_t radio_ms, sequence;
    int32_t targets[4];
    uint8_t initialized, radio_seen, permission, need_disarm, loss_rearm;
    uint8_t candidate_count, arm_count, neutral_count, manual_ready, link_lost;
} Drive_State_t;
static volatile Drive_State_t s;

static uint32_t Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

/* Caller holds PRIMASK; only bounded memory/GPIO/PWM updates. */
static void Revoke(void)
{
    uint8_t was_active = (uint8_t)(s.permission || s.manual_ready || RDK_UART_IsControlEnabled());
    s.permission = s.manual_ready = s.neutral_count = s.arm_count = 0U;
    for (uint8_t i = 0U; i < 4U; ++i) s.targets[i] = 0;
    RDK_UART_SetControlEnabled(0U);
    if (was_active != 0U) Motor_ClosedLoopEmergencyStop();
}

static void Select(Drive_Mode_t mode)
{
    if (s.mode != mode)
    {
        Revoke();
        s.mode = mode;
        s.need_disarm = 1U;
    }
}

static void LinkLost(void)
{
    Revoke();
    s.mode = DRIVE_STOP;
    s.need_disarm = s.link_lost = 1U;
    s.loss_rearm = 1U;
    s.candidate_count = 0U;
}

void DriveControl_Init(void)
{
    uint32_t mask = Lock();
    s.mode = DRIVE_HOST;
    s.candidate = DRIVE_STOP;
    s.radio_seen = s.need_disarm = s.link_lost = s.loss_rearm = 0U;
    s.candidate_count = s.arm_count = s.neutral_count = s.manual_ready = 0U;
    s.sequence = s.radio_ms = 0U;
    for (uint8_t i = 0U; i < 4U; ++i) s.targets[i] = 0;
    RDK_UART_SetControlEnabled(0U);
    RDK_UART_GrantFreshControl();
    s.permission = 1U;
    s.initialized = 1U;
    Motor_ClosedLoopEmergencyStop();
    __set_PRIMASK(mask);
}

static uint8_t Centered(const ELRS_Snapshot_t *input)
{
    const uint8_t stick[3] = {0U, 1U, 3U};
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        int32_t displacement = (int32_t)input->channels[stick[i]] - STICK_MID;
        if (displacement > STICK_CENTER_TOLERANCE || displacement < -STICK_CENTER_TOLERANCE) return 0U;
    }
    return 1U;
}

void DriveControl_Process(void)
{
    if (s.initialized == 0U) return;
    ELRS_Snapshot_t input;
    uint8_t valid = ELRS_GetSnapshot(&input);
    int32_t targets[4] = {0};
    if (valid != 0U && (s.radio_seen == 0U || s.sequence != input.sequence))
        Mecanum_CalcMotorTargets(input.control.vx_cps, input.control.vy_cps,
            input.control.wz_cps, DRIVE_REMOTE_WHEEL_LIMIT_CPS, targets);
    uint32_t mask = Lock();
    uint32_t now = HAL_GetTick();
    if (valid == 0U || ELRS_IsLinkActive(now) == 0U ||
        (uint32_t)(now - input.received_ms) >= DRIVE_RADIO_TIMEOUT_MS)
    {
        if (s.radio_seen != 0U) LinkLost();
        __set_PRIMASK(mask);
        return;
    }
    if (s.radio_seen == 0U)
    {
        Revoke();
        s.radio_seen = s.need_disarm = 1U;
    }
    if (s.sequence == input.sequence) { __set_PRIMASK(mask); return; }
    s.sequence = input.sequence;
    s.radio_ms = input.received_ms; /* Never refresh from foreground poll time. */
    s.link_lost = 0U;
    Drive_Mode_t desired = input.channels[5] <= MODE_LOW_MAX ? DRIVE_HOST :
        (input.channels[5] >= MODE_HIGH_MIN ? DRIVE_REMOTE : DRIVE_STOP);
    if (desired == DRIVE_STOP)
    {
        Revoke();
        Select(DRIVE_STOP);
        s.candidate_count = 0U;
        s.need_disarm = 1U;
        __set_PRIMASK(mask);
        return;
    }
    if (desired != s.mode)
    {
        Select(DRIVE_STOP); /* Stop on the FIRST different-position frame. */
        if (s.candidate != desired) { s.candidate = desired; s.candidate_count = 1U; }
        else if (s.candidate_count < STABLE_FRAMES) ++s.candidate_count;
        if (s.candidate_count >= STABLE_FRAMES) Select(desired);
    }
    else { s.candidate = desired; s.candidate_count = STABLE_FRAMES; }

    if (input.channels[4] <= ARM_LOW_MAX)
    {
        s.need_disarm = 0U;
        s.loss_rearm = 0U;
        Revoke();
    }
    else if (s.mode == DRIVE_STOP || s.loss_rearm != 0U ||
             input.channels[4] < ARM_HIGH_MIN || input.control.armed == 0U)
        Revoke();
    else
    {
        if (s.arm_count < STABLE_FRAMES) ++s.arm_count;
        if (s.mode == DRIVE_HOST)
        {
            if (s.arm_count >= STABLE_FRAMES)
            {
                s.permission = 1U;
                /* CH6 HOST + CH5 high is explicit permission. Do not require
                 * an extra arm-switch cycle or a host-side zero command. */
                RDK_UART_GrantFreshControl();
            }
            __set_PRIMASK(mask);
            return;
        }
        if (s.need_disarm != 0U)
        {
            Revoke();
            __set_PRIMASK(mask);
            return;
        }
        if (s.manual_ready == 0U)
        {
            if (Centered(&input) != 0U) ++s.neutral_count;
            else s.neutral_count = 0U;
            if (s.neutral_count < STABLE_FRAMES || s.arm_count < STABLE_FRAMES)
            { __set_PRIMASK(mask); return; }
            s.manual_ready = 1U;
        }
        s.permission = 1U;
        for (uint8_t i = 0U; i < 4U; ++i) s.targets[i] = targets[i];
    }
    __set_PRIMASK(mask);
}

static uint8_t RadioExpired(uint32_t now)
{
    return (uint8_t)(s.radio_seen != 0U &&
        (ELRS_IsLinkActive(now) == 0U || (uint32_t)(now - s.radio_ms) >= DRIVE_RADIO_TIMEOUT_MS));
}

void DriveControl_WatchdogTick(uint32_t now)
{
    if (s.initialized == 0U) return;
    uint32_t mask = Lock();
    if (RadioExpired(now) != 0U) LinkLost();
    if (s.mode == DRIVE_HOST && s.permission != 0U) RDK_UART_WatchdogTick(now);
    __set_PRIMASK(mask);
}

uint8_t DriveControl_ControlTick(uint32_t now)
{
    uint32_t mask = Lock();
    uint8_t ready = 0U;
    if (s.initialized != 0U && RadioExpired(now) != 0U) LinkLost();
    if (s.initialized != 0U && s.permission != 0U)
    {
        if (s.mode == DRIVE_HOST) ready = RDK_UART_ControlTick(now);
        else if (s.mode == DRIVE_REMOTE && s.manual_ready != 0U)
        {
            for (uint8_t i = 0U; i < 4U; ++i) Motor_ClosedLoopSetTarget((Motor_ID_t)i, s.targets[i]);
            ready = 1U;
        }
    }
    if (ready == 0U) Motor_ClosedLoopEmergencyStop();
    __set_PRIMASK(mask);
    return ready;
}

Drive_Mode_t DriveControl_GetMode(void) { return s.mode; }
uint8_t DriveControl_IsEnabled(void) { return s.permission; }

const char *DriveControl_GetLabel(void)
{
    if (s.mode == DRIVE_STOP) return s.link_lost != 0U ? "CTRL:LINK LOST" : "CTRL:STOP";
    if (s.mode == DRIVE_REMOTE) return s.permission != 0U ? "CTRL:REMOTE" : "CTRL:REMOTE LOCK";
    if (s.permission == 0U) return "CTRL:HOST LOCK";
    return RDK_UART_IsCommandReady() != 0U ? "CTRL:HOST" : "CTRL:HOST WAIT";
}
