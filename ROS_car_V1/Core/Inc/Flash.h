#ifndef __FLASH_H
#define __FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "Menu.h"

HAL_StatusTypeDef Flash_PID_LoadOrDefault(PID_Params_t pid_table[4]);
HAL_StatusTypeDef Flash_PID_Save(const PID_Params_t pid_table[4]);
HAL_StatusTypeDef Flash_PID_SaveIfChanged(const PID_Params_t pid_table[4]);
HAL_StatusTypeDef Flash_PID_ResetToDefault(PID_Params_t pid_table[4]);
HAL_StatusTypeDef Flash_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_H */
