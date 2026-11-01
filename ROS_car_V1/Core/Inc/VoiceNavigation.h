#ifndef __VOICE_NAVIGATION_H
#define __VOICE_NAVIGATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define NAVIGATION_TARGET_ORIGIN     0U
#define NAVIGATION_TARGET_POINT_1    1U
#define NAVIGATION_TARGET_POINT_2    2U
#define NAVIGATION_TARGET_POINT_3    3U
#define NAVIGATION_TARGET_NONE       0xFFU

typedef struct
{
  float x_m;
  float y_m;
} Navigation_Point_t;

extern volatile uint8_t g_navigation_target;

HAL_StatusTypeDef VoiceNavigation_CheckDevice(void);
HAL_StatusTypeDef VoiceNavigation_Poll(void);
void VoiceNavigation_ClearTarget(void);
uint8_t VoiceNavigation_GetTargetPoint(uint8_t target, Navigation_Point_t *point);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_NAVIGATION_H */
