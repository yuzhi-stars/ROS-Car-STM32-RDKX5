#include "IMU.h"
#include "spi.h"

#define LSM6DSR_WHO_AM_I       0x0FU
#define LSM6DSR_CTRL1_XL       0x10U
#define LSM6DSR_CTRL2_G        0x11U
#define LSM6DSR_CTRL3_C        0x12U
#define LSM6DSR_STATUS_REG     0x1EU
#define LSM6DSR_OUTX_L_G       0x22U
#define LSM6DSR_OUTX_L_A       0x28U
#define LSM6DSR_ID             0x6BU

#define LIS3MDL_WHO_AM_I       0x0FU
#define LIS3MDL_CTRL_REG1      0x20U
#define LIS3MDL_CTRL_REG2      0x21U
#define LIS3MDL_CTRL_REG3      0x22U
#define LIS3MDL_CTRL_REG4      0x23U
#define LIS3MDL_CTRL_REG5      0x24U
#define LIS3MDL_OUT_X_L        0x28U
#define LIS3MDL_ID             0x3DU

#define SPI_READ               0x80U
#define LIS3MDL_AUTO_INCREMENT 0x40U
#define IMU_SPI_MAX_READ_LEN   6U

static void LSM6DSR_Select(void)
{
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_RESET);
}

static void LSM6DSR_Unselect(void)
{
  HAL_GPIO_WritePin(IMU_CS_GPIO_Port, IMU_CS_Pin, GPIO_PIN_SET);
}

static void LIS3MDL_Select(void)
{
  HAL_GPIO_WritePin(LIS3_CS_GPIO_Port, LIS3_CS_Pin, GPIO_PIN_RESET);
}

static void LIS3MDL_Unselect(void)
{
  HAL_GPIO_WritePin(LIS3_CS_GPIO_Port, LIS3_CS_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef SPI_WriteReg(void (*select)(void), void (*unselect)(void), uint8_t reg, uint8_t value)
{
  HAL_StatusTypeDef status;
  uint8_t tx_data[2] = {reg, value};

  select();
  status = HAL_SPI_Transmit(&hspi2, tx_data, sizeof(tx_data), 100);
  unselect();

  return status;
}

static HAL_StatusTypeDef SPI_ReadRegs(void (*select)(void), void (*unselect)(void), uint8_t cmd, uint8_t *data, uint16_t len)
{
  HAL_StatusTypeDef status;
  uint8_t tx_data[IMU_SPI_MAX_READ_LEN + 1U] = {0};
  uint8_t rx_data[IMU_SPI_MAX_READ_LEN + 1U] = {0};

  if ((data == NULL) || (len == 0U) || (len > IMU_SPI_MAX_READ_LEN))
  {
    return HAL_ERROR;
  }

  tx_data[0] = cmd;

  select();
  for (volatile uint32_t delay = 0; delay < 80U; delay++)
  {
    __NOP();
  }
  status = HAL_SPI_TransmitReceive(&hspi2, tx_data, rx_data, (uint16_t)(len + 1U), 100);
  unselect();

  if (status == HAL_OK)
  {
    for (uint16_t i = 0; i < len; i++)
    {
      data[i] = rx_data[i + 1U];
    }
  }

  return status;
}
static HAL_StatusTypeDef LSM6DSR_WriteReg(uint8_t reg, uint8_t value)
{
  return SPI_WriteReg(LSM6DSR_Select, LSM6DSR_Unselect, (uint8_t)(reg & 0x7FU), value);
}

static HAL_StatusTypeDef LSM6DSR_ReadRegs(uint8_t reg, uint8_t *data, uint16_t len)
{
  return SPI_ReadRegs(LSM6DSR_Select, LSM6DSR_Unselect, (uint8_t)(reg | SPI_READ), data, len);
}

static HAL_StatusTypeDef LIS3MDL_WriteReg(uint8_t reg, uint8_t value)
{
  return SPI_WriteReg(LIS3MDL_Select, LIS3MDL_Unselect, (uint8_t)(reg & 0x3FU), value);
}

static HAL_StatusTypeDef LIS3MDL_ReadRegs(uint8_t reg, uint8_t *data, uint16_t len)
{
  uint8_t cmd = (uint8_t)(reg | SPI_READ);
  if (len > 1U)
  {
    cmd |= LIS3MDL_AUTO_INCREMENT;
  }
  return SPI_ReadRegs(LIS3MDL_Select, LIS3MDL_Unselect, cmd, data, len);
}

HAL_StatusTypeDef IMU_Init(void)
{
  HAL_StatusTypeDef status;

  LSM6DSR_Unselect();
  LIS3MDL_Unselect();
  HAL_Delay(20);

  status = LSM6DSR_WriteReg(LSM6DSR_CTRL3_C, 0x44U);  /* BDU=1, IF_INC=1 */
  if (status != HAL_OK) return status;
  status = LSM6DSR_WriteReg(LSM6DSR_CTRL2_G, 0x40U);  /* Gyro 104 Hz, 250 dps */
  if (status != HAL_OK) return status;
  status = LSM6DSR_WriteReg(LSM6DSR_CTRL1_XL, 0x40U); /* Accel 104 Hz, 2 g */
  if (status != HAL_OK) return status;

  status = LIS3MDL_WriteReg(LIS3MDL_CTRL_REG1, 0x62U); /* Ultra-high-performance XY, FAST_ODR, about 155 Hz */
  if (status != HAL_OK) return status;
  status = LIS3MDL_WriteReg(LIS3MDL_CTRL_REG2, 0x00U); /* +/- 4 gauss */
  if (status != HAL_OK) return status;
  status = LIS3MDL_WriteReg(LIS3MDL_CTRL_REG3, 0x00U); /* Continuous-conversion mode */
  if (status != HAL_OK) return status;
  status = LIS3MDL_WriteReg(LIS3MDL_CTRL_REG4, 0x0CU); /* Ultra-high-performance Z */
  if (status != HAL_OK) return status;
  status = LIS3MDL_WriteReg(LIS3MDL_CTRL_REG5, 0x40U); /* BDU=1 */

  return status;
}

IMU_Id_t IMU_ReadID(void)
{
  IMU_Id_t id = {0};

  (void)LSM6DSR_ReadRegs(LSM6DSR_WHO_AM_I, &id.lsm6dsr_who_am_i, 1);
  (void)LIS3MDL_ReadRegs(LIS3MDL_WHO_AM_I, &id.lis3mdl_who_am_i, 1);

  return id;
}

HAL_StatusTypeDef IMU_ReadGyroRaw(IMU_AxisRaw_t *gyro)
{
  uint8_t data[6];
  HAL_StatusTypeDef status;

  if (gyro == NULL)
  {
    return HAL_ERROR;
  }

  status = LSM6DSR_ReadRegs(LSM6DSR_OUTX_L_G, data, sizeof(data));
  if (status == HAL_OK)
  {
    gyro->x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
    gyro->y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
    gyro->z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
  }

  return status;
}

HAL_StatusTypeDef IMU_ReadAccelRaw(IMU_AxisRaw_t *accel)
{
  uint8_t data[6];
  HAL_StatusTypeDef status;

  if (accel == NULL)
  {
    return HAL_ERROR;
  }

  status = LSM6DSR_ReadRegs(LSM6DSR_OUTX_L_A, data, sizeof(data));
  if (status == HAL_OK)
  {
    accel->x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
    accel->y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
    accel->z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
  }

  return status;
}
HAL_StatusTypeDef IMU_ReadMotionRaw(IMU_AxisRaw_t *gyro,
                                   IMU_AxisRaw_t *accel,
                                   uint8_t *fresh_flags)
{
  uint8_t data_ready = 0U;
  HAL_StatusTypeDef result = HAL_OK;
  if (fresh_flags == NULL) return HAL_ERROR;
  *fresh_flags = 0U;
  if ((gyro == NULL) || (accel == NULL)) return HAL_ERROR;
  result = LSM6DSR_ReadRegs(LSM6DSR_STATUS_REG, &data_ready, 1U);
  if (result != HAL_OK) return result;

  /* LSM6DSR STATUS_REG: bit1=GDA, bit0=XLDA. Reading the output
   * consumes the corresponding new-data indication; cached data is not valid. */
  if ((data_ready & IMU_MOTION_GYRO_FRESH) != 0U)
  {
    if (IMU_ReadGyroRaw(gyro) == HAL_OK) *fresh_flags |= IMU_MOTION_GYRO_FRESH;
    else result = HAL_ERROR;
  }
  if ((data_ready & IMU_MOTION_ACCEL_FRESH) != 0U)
  {
    if (IMU_ReadAccelRaw(accel) == HAL_OK) *fresh_flags |= IMU_MOTION_ACCEL_FRESH;
    else result = HAL_ERROR;
  }
  return result;
}

HAL_StatusTypeDef IMU_ReadMagRaw(IMU_AxisRaw_t *mag)
{
  uint8_t data[6];
  HAL_StatusTypeDef status;

  if (mag == NULL)
  {
    return HAL_ERROR;
  }

  status = LIS3MDL_ReadRegs(LIS3MDL_OUT_X_L, data, sizeof(data));
  if (status == HAL_OK)
  {
    mag->x = (int16_t)((uint16_t)data[1] << 8 | data[0]);
    mag->y = (int16_t)((uint16_t)data[3] << 8 | data[2]);
    mag->z = (int16_t)((uint16_t)data[5] << 8 | data[4]);
  }

  return status;
}

