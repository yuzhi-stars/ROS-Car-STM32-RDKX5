#ifndef __MECANUM_H
#define __MECANUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/*
 * Mecanum wheel layout used by this project:
 *
 *   motor2: left-front   -> positive wheel force points right-forward
 *   motor1: right-front  -> positive wheel force points left-forward
 *   motor4: left-rear    -> positive wheel force points left-forward
 *   motor3: right-rear   -> positive wheel force points right-forward
 *
 * Command coordinate:
 *   vx_cps: forward is positive
 *   vy_cps: left is positive
 *   wz_cps: counterclockwise yaw is positive
 *
 * Unit:
 *   The input and output use the same unit as Motor_ClosedLoopSetTarget(),
 *   currently encoder counts per second.
 */
void Mecanum_SetVelocity(int32_t vx_cps, int32_t vy_cps, int32_t wz_cps);
void Mecanum_SetVelocityLimited(int32_t vx_cps,
                                int32_t vy_cps,
                                int32_t wz_cps,
                                int32_t limit_cps);
void Mecanum_Stop(void);
/* Pure calculation, output in Motor_ID order FL,FR,RL,RR; no target writes. */
void Mecanum_CalcMotorTargets(int32_t vx_cps, int32_t vy_cps, int32_t wz_cps,
    int32_t limit_cps, int32_t targets[4]);
void Mecanum_CalcWheelTargets(int32_t vx_cps,
                              int32_t vy_cps,
                              int32_t wz_cps,
                              int32_t targets[4]);
void Mecanum_NormalizeTargets(int32_t targets[4], int32_t limit_cps);

#ifdef __cplusplus
}
#endif

#endif /* __MECANUM_H */

