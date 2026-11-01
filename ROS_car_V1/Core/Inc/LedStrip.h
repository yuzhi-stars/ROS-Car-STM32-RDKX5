#ifndef __LED_STRIP_H
#define __LED_STRIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define LED_STRIP_LED_COUNT  20U

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} LedStrip_Color_t;

void LedStrip_Init(void);
void LedStrip_SetPixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
void LedStrip_Fill(uint8_t r, uint8_t g, uint8_t b);
HAL_StatusTypeDef LedStrip_Show(void);
uint8_t LedStrip_IsBusy(void);
void LedStrip_TestRainbowStep(void);
void LedStrip_AlternateGroupsRainbowStep(void);
void LedStrip_BreathStep(void);
void LedStrip_RainbowFlowStep(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_STRIP_H */


