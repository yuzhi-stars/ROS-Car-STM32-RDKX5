#include "INA219.h"
#include "i2c.h"

#define INA219_I2C_ADDR           (INA219_I2C_ADDR_7BIT << 1U)

#define INA219_REG_CONFIG         0x00U
#define INA219_REG_SHUNT_VOLTAGE  0x01U
#define INA219_REG_BUS_VOLTAGE    0x02U
#define INA219_REG_POWER          0x03U
#define INA219_REG_CURRENT        0x04U
#define INA219_REG_CALIBRATION    0x05U

/*
 * Config: 32V bus range, +/-320mV shunt range, 12-bit bus/shunt ADC,
 * continuous shunt and bus measurement.
 */
#define INA219_CONFIG_VALUE       0x399FU

/*
 * Calibration = 0.04096 / (Current_LSB * Rshunt)
 * Current_LSB = 1mA/bit, Rshunt = 10mOhm -> 4096.
 * Current register LSB = 1mA, power register LSB = 20mW.
 */
#define INA219_CALIBRATION_VALUE  4096U

static HAL_StatusTypeDef INA219_WriteRegister(uint8_t reg, uint16_t value);
static HAL_StatusTypeDef INA219_ReadRegister(uint8_t reg, uint16_t *value);
static HAL_StatusTypeDef INA219_EnsureCalibration(void);

HAL_StatusTypeDef INA219_Init(void)
{
  HAL_StatusTypeDef status;

  status = INA219_WriteRegister(INA219_REG_CONFIG, INA219_CONFIG_VALUE);
  if (status != HAL_OK)
  {
    return status;
  }

  return INA219_WriteRegister(INA219_REG_CALIBRATION, INA219_CALIBRATION_VALUE);
}

HAL_StatusTypeDef INA219_IsReady(void)
{
  return HAL_I2C_IsDeviceReady(&hi2c1, INA219_I2C_ADDR, 3U, 10U);
}

HAL_StatusTypeDef INA219_ReadBusVoltage_mV(int32_t *voltage_mV)
{
  HAL_StatusTypeDef status;
  uint16_t raw;

  if (voltage_mV == NULL)
  {
    return HAL_ERROR;
  }

  status = INA219_ReadRegister(INA219_REG_BUS_VOLTAGE, &raw);
  if (status != HAL_OK)
  {
    return status;
  }

  /* Bus voltage register bits [15:3], LSB = 4mV. */
  *voltage_mV = (int32_t)((raw >> 3U) * 4U);
  return HAL_OK;
}

HAL_StatusTypeDef INA219_ReadShuntVoltage_uV(int32_t *voltage_uV)
{
  HAL_StatusTypeDef status;
  uint16_t raw;
  int16_t signed_raw;

  if (voltage_uV == NULL)
  {
    return HAL_ERROR;
  }

  status = INA219_ReadRegister(INA219_REG_SHUNT_VOLTAGE, &raw);
  if (status != HAL_OK)
  {
    return status;
  }

  signed_raw = (int16_t)raw;
  /* Shunt voltage register LSB = 10uV. */
  *voltage_uV = (int32_t)signed_raw * 10L;
  return HAL_OK;
}

HAL_StatusTypeDef INA219_ReadCurrent_mA(int32_t *current_mA)
{
  HAL_StatusTypeDef status;
  uint16_t raw;
  int16_t signed_raw;

  if (current_mA == NULL)
  {
    return HAL_ERROR;
  }

  status = INA219_EnsureCalibration();
  if (status != HAL_OK)
  {
    return status;
  }

  status = INA219_ReadRegister(INA219_REG_CURRENT, &raw);
  if (status != HAL_OK)
  {
    return status;
  }

  signed_raw = (int16_t)raw;
  *current_mA = (int32_t)signed_raw * INA219_CURRENT_LSB_MA;
  return HAL_OK;
}

HAL_StatusTypeDef INA219_ReadPower_mW(int32_t *power_mW)
{
  HAL_StatusTypeDef status;
  uint16_t raw;

  if (power_mW == NULL)
  {
    return HAL_ERROR;
  }

  status = INA219_EnsureCalibration();
  if (status != HAL_OK)
  {
    return status;
  }

  status = INA219_ReadRegister(INA219_REG_POWER, &raw);
  if (status != HAL_OK)
  {
    return status;
  }

  *power_mW = (int32_t)raw * INA219_POWER_LSB_MW;
  return HAL_OK;
}

HAL_StatusTypeDef INA219_ReadAll(INA219_Data_t *data)
{
  HAL_StatusTypeDef status;

  if (data == NULL)
  {
    return HAL_ERROR;
  }

  status = INA219_ReadBusVoltage_mV(&data->bus_voltage_mV);
  if (status != HAL_OK)
  {
    return status;
  }

  status = INA219_ReadShuntVoltage_uV(&data->shunt_voltage_uV);
  if (status != HAL_OK)
  {
    return status;
  }

  status = INA219_ReadCurrent_mA(&data->current_mA);
  if (status != HAL_OK)
  {
    return status;
  }

  return INA219_ReadPower_mW(&data->power_mW);
}

static HAL_StatusTypeDef INA219_WriteRegister(uint8_t reg, uint16_t value)
{
  uint8_t tx_data[2];

  tx_data[0] = (uint8_t)(value >> 8U);
  tx_data[1] = (uint8_t)(value & 0xFFU);

  return HAL_I2C_Mem_Write(&hi2c1,
                           INA219_I2C_ADDR,
                           reg,
                           I2C_MEMADD_SIZE_8BIT,
                           tx_data,
                           sizeof(tx_data),
                           20U);
}

static HAL_StatusTypeDef INA219_ReadRegister(uint8_t reg, uint16_t *value)
{
  HAL_StatusTypeDef status;
  uint8_t rx_data[2];

  if (value == NULL)
  {
    return HAL_ERROR;
  }

  status = HAL_I2C_Mem_Read(&hi2c1,
                            INA219_I2C_ADDR,
                            reg,
                            I2C_MEMADD_SIZE_8BIT,
                            rx_data,
                            sizeof(rx_data),
                            20U);
  if (status != HAL_OK)
  {
    return status;
  }

  *value = (uint16_t)(((uint16_t)rx_data[0] << 8U) | rx_data[1]);
  return HAL_OK;
}

static HAL_StatusTypeDef INA219_EnsureCalibration(void)
{
  /* Re-write calibration before current/power reads. INA219 can clear it after reset. */
  return INA219_WriteRegister(INA219_REG_CALIBRATION, INA219_CALIBRATION_VALUE);
}
