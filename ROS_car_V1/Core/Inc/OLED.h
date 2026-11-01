#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define OLED_I2C_ADDR   (0x3CU << 1)
#define OLED_WIDTH      128U
#define OLED_HEIGHT     64U

void OLED_Init(void);
/* All drawing calls update RAM only. Process starts at most one 32-byte DMA
 * chunk/command, never waits for it. Panel stays off until DisplayOn(). */
void OLED_Process(uint32_t now_ms);
uint8_t OLED_I2CAvailable(void);
void OLED_TxCpltCallback(I2C_HandleTypeDef *i2c);
void OLED_ErrorCallback(I2C_HandleTypeDef *i2c);
typedef struct { uint32_t chunks, errors, recoveries; } OLED_Statistics_t;
void OLED_GetStatistics(OLED_Statistics_t *statistics);
void OLED_Clear(void);
void OLED_DisplayOn(void);
void OLED_DisplayOff(void);
void OLED_SetCursor(uint8_t page, uint8_t column);
void OLED_ShowChar(uint8_t page, uint8_t column, char ch);
void OLED_ShowString(uint8_t page, uint8_t column, const char *str);
void OLED_ClearLine(uint8_t page);
void OLED_ShowCharInverted(uint8_t page, uint8_t column, char ch);
void OLED_ShowStringInverted(uint8_t page, uint8_t column, const char *str);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */

