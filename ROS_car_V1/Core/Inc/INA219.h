#ifndef __INA219_H
#define __INA219_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define INA219_I2C_ADDR_7BIT      0x40U
#define INA219_SHUNT_RESISTOR_MOHM 10U
#define INA219_CURRENT_LSB_MA      1L
#define INA219_POWER_LSB_MW        20L

typedef struct
{
  int32_t bus_voltage_mV;
  int32_t shunt_voltage_uV;
  int32_t current_mA;
  int32_t power_mW;
} INA219_Data_t;

HAL_StatusTypeDef INA219_Init(void);
HAL_StatusTypeDef INA219_IsReady(void);
HAL_StatusTypeDef INA219_ReadBusVoltage_mV(int32_t *voltage_mV);
HAL_StatusTypeDef INA219_ReadShuntVoltage_uV(int32_t *voltage_uV);
HAL_StatusTypeDef INA219_ReadCurrent_mA(int32_t *current_mA);
HAL_StatusTypeDef INA219_ReadPower_mW(int32_t *power_mW);
HAL_StatusTypeDef INA219_ReadAll(INA219_Data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* __INA219_H */
