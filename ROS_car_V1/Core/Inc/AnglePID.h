#ifndef __ANGLE_PID_H
#define __ANGLE_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void AnglePID_Init(void);
void AnglePID_SetYawDeg(float yaw_deg);
void AnglePID_SetTargetDeg(float target_deg);
void AnglePID_ResetTargetToCurrent(void);
int32_t AnglePID_Update(int32_t manual_wz_cps);
int32_t AnglePID_UpdateWithTarget(float target_deg, float current_deg);
float AnglePID_GetTargetDeg(void);
float AnglePID_GetErrorDeg(void);

#ifdef __cplusplus
}
#endif

#endif /* __ANGLE_PID_H */
