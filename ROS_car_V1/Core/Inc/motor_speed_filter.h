#ifndef MOTOR_SPEED_FILTER_H
#define MOTOR_SPEED_FILTER_H
#include <math.h>

/* Keep the existing alpha=0.35, retaining the fractional state. Integer state
 * stalls at +/-2 cps when raw becomes zero because its correction truncates. */
static inline float MotorSpeedFilter_Update(float previous, float raw_cps)
{
    float next = previous + 0.35f * (raw_cps - previous);
    if (raw_cps == 0.0f && fabsf(next) < 0.001f) next = 0.0f;
    return next;
}
#endif
