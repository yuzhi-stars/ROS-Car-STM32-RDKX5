#ifndef __ODOM_H
#define __ODOM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct
{
  float x_m;
  float y_m;
  float yaw_rad;
  float vx_mps;
  float vy_mps;
  float wz_radps;
} Odom_State_t;

void Odom_Init(void);
void Odom_Reset(void);
void Odom_Update20ms(void);
void Odom_GetState(Odom_State_t *state);
void Odom_GetPose(float *x_m, float *y_m, float *yaw_rad);
void Odom_SetPose(float x_m, float y_m, float yaw_rad);
void Odom_GetVelocity(float *vx_mps, float *vy_mps, float *wz_radps);

#ifdef __cplusplus
}
#endif

#endif /* __ODOM_H */
