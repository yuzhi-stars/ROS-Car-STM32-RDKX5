#ifndef __IMU_H
#define __IMU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct
{
  int16_t x;
  int16_t y;
  int16_t z;
} IMU_AxisRaw_t;

typedef struct
{
  uint8_t lsm6dsr_who_am_i;
  uint8_t lis3mdl_who_am_i;
} IMU_Id_t;

HAL_StatusTypeDef IMU_Init(void);
IMU_Id_t IMU_ReadID(void);
HAL_StatusTypeDef IMU_ReadGyroRaw(IMU_AxisRaw_t *gyro);
HAL_StatusTypeDef IMU_ReadAccelRaw(IMU_AxisRaw_t *accel);
/* STATUS_REG new-data bits. Clear flags mean no new sample or a read error. */
#define IMU_MOTION_ACCEL_FRESH 0x01U
#define IMU_MOTION_GYRO_FRESH  0x02U
HAL_StatusTypeDef IMU_ReadMotionRaw(IMU_AxisRaw_t *gyro,
                                   IMU_AxisRaw_t *accel,
                                   uint8_t *fresh_flags);
HAL_StatusTypeDef IMU_ReadMagRaw(IMU_AxisRaw_t *mag);

#ifdef __cplusplus
}
#endif

#endif /* __IMU_H */
