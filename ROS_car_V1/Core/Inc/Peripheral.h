#ifndef __PERIPHERAL_H
#define __PERIPHERAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* PC13 user LED is active-low. */
#define LED_ON()  HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_RESET)
#define LED_OFF() HAL_GPIO_WritePin(USER_LED_GPIO_Port, USER_LED_Pin, GPIO_PIN_SET)

/* PC3 buzzer is active-high. */
#define BUZZER_ON()  HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET)
#define BUZZER_OFF() HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET)

void Peripheral_LED_Init(void);
void Peripheral_LED_Blink_1s(void);
void Peripheral_BUZZER_Init(void);
void Peripheral_BUZZER_Beep(uint32_t duration_ms);
void Peripheral_LED_BUZZER_Blink_1s(void);

/*
 * Non-blocking buzzer rhythm player.
 * times       : beep count. One beep means on_ms sound + interval_ms silence.
 * on_ms       : sound duration for each beep.
 * interval_ms : silence duration between two beeps.
 */
void Peripheral_BUZZER_Play(uint16_t times,
                            uint16_t on_ms,
                            uint16_t interval_ms);
void Peripheral_BUZZER_Stop(void);
void Peripheral_BUZZER_ForceOn(void);
void Peripheral_BUZZER_AlarmOn(uint16_t on_ms, uint16_t interval_ms);
void Peripheral_BUZZER_AlarmOff(void);
uint8_t Peripheral_BUZZER_IsBusy(void);
void Peripheral_BUZZER_Tick20ms(void);

#ifdef __cplusplus
}
#endif

#endif /* __PERIPHERAL_H */



