#ifndef __MENU_H
#define __MENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "imu_calibration.h"
typedef struct
{
  float kp;
  float ki;
  float kd;
} PID_Params_t;

typedef struct
{
  float *yaw_deg;
  int32_t *motor_encoder_speeds;
  int32_t *ina219_bus_voltage_mV;
  int32_t *ina219_current_mA;
  const IMU_Calibration_t *imu_calibration;
} Menu_Status_t;

void Menu_Init(PID_Params_t *motor_pid_table, Menu_Status_t *status_ptr);
void Menu_Process(uint32_t now_ms);
uint8_t Menu_IsTargetControlActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __MENU_H */




