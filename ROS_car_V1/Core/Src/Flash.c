#include "Flash.h"
#include "i2c.h"
#include <string.h>

/*
 * MB85RC64TAPNF-G-BDERE1 is an I2C FRAM, not internal MCU Flash.
 * Capacity: 64 Kbit = 8 KByte. I2C base address is 0x50 when A2/A1/A0 are low.
 */
#define FLASH_FRAM_I2C_ADDR_7BIT       0x50U
#define FLASH_FRAM_I2C_ADDR            (FLASH_FRAM_I2C_ADDR_7BIT << 1)
#define FLASH_FRAM_PID_ADDR            0x0000U
#define FLASH_FRAM_TIMEOUT_MS          100U

#define FLASH_PID_MAGIC                0x50494431UL  /* "PID1" */
#define FLASH_PID_VERSION              1U
#define FLASH_PID_MOTOR_COUNT          4U

#pragma pack(push, 1)
typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t motor_count;
  PID_Params_t pid[FLASH_PID_MOTOR_COUNT];
  uint32_t crc;
} Flash_PID_Record_t;
#pragma pack(pop)

static PID_Params_t cached_pid[FLASH_PID_MOTOR_COUNT];
static uint8_t cache_valid = 0U;
static uint8_t fram_available = 0U;

static uint32_t Flash_CalcChecksum(const uint8_t *data, uint32_t len);
static uint8_t Flash_PID_IsDifferent(const PID_Params_t a[FLASH_PID_MOTOR_COUNT], const PID_Params_t b[FLASH_PID_MOTOR_COUNT]);
static void Flash_PID_SetDefault(PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT]);

HAL_StatusTypeDef Flash_IsReady(void)
{
  HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c1, FLASH_FRAM_I2C_ADDR, 3U, FLASH_FRAM_TIMEOUT_MS);
  fram_available = (status == HAL_OK) ? 1U : 0U;
  return status;
}

HAL_StatusTypeDef Flash_PID_LoadOrDefault(PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT])
{
  Flash_PID_Record_t record;
  HAL_StatusTypeDef status;
  uint32_t crc;

  if (pid_table == NULL)
  {
    return HAL_ERROR;
  }

  status = Flash_IsReady();
  if (status != HAL_OK)
  {
    Flash_PID_SetDefault(pid_table);
    memcpy(cached_pid, pid_table, sizeof(cached_pid));
    cache_valid = 1U;
    return status;
  }

  status = HAL_I2C_Mem_Read(&hi2c1,
                            FLASH_FRAM_I2C_ADDR,
                            FLASH_FRAM_PID_ADDR,
                            I2C_MEMADD_SIZE_16BIT,
                            (uint8_t *)&record,
                            sizeof(record),
                            FLASH_FRAM_TIMEOUT_MS);

  if (status == HAL_OK)
  {
    crc = Flash_CalcChecksum((const uint8_t *)&record, (uint32_t)(sizeof(record) - sizeof(record.crc)));

    if ((record.magic == FLASH_PID_MAGIC) &&
        (record.version == FLASH_PID_VERSION) &&
        (record.motor_count == FLASH_PID_MOTOR_COUNT) &&
        (record.crc == crc))
    {
      memcpy(pid_table, record.pid, sizeof(record.pid));
      memcpy(cached_pid, record.pid, sizeof(cached_pid));
      cache_valid = 1U;
      return HAL_OK;
    }
  }

  Flash_PID_SetDefault(pid_table);
  memcpy(cached_pid, pid_table, sizeof(cached_pid));
  cache_valid = 1U;

  if (status == HAL_OK)
  {
    /* FRAM is reachable but stored data is invalid. Initialize it with defaults. */
    return Flash_PID_Save(pid_table);
  }

  return status;
}

HAL_StatusTypeDef Flash_PID_Save(const PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT])
{
  Flash_PID_Record_t record;
  HAL_StatusTypeDef status;

  if (pid_table == NULL)
  {
    return HAL_ERROR;
  }

  record.magic = FLASH_PID_MAGIC;
  record.version = FLASH_PID_VERSION;
  record.motor_count = FLASH_PID_MOTOR_COUNT;
  memcpy(record.pid, pid_table, sizeof(record.pid));
  record.crc = Flash_CalcChecksum((const uint8_t *)&record, (uint32_t)(sizeof(record) - sizeof(record.crc)));

  if (fram_available == 0U)
  {
    return HAL_ERROR;
  }

  status = HAL_I2C_Mem_Write(&hi2c1,
                             FLASH_FRAM_I2C_ADDR,
                             FLASH_FRAM_PID_ADDR,
                             I2C_MEMADD_SIZE_16BIT,
                             (uint8_t *)&record,
                             sizeof(record),
                             FLASH_FRAM_TIMEOUT_MS);

  if (status == HAL_OK)
  {
    memcpy(cached_pid, pid_table, sizeof(cached_pid));
    cache_valid = 1U;
  }

  return status;
}

HAL_StatusTypeDef Flash_PID_ResetToDefault(PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT])
{
  HAL_StatusTypeDef status;

  if (pid_table == NULL)
  {
    return HAL_ERROR;
  }

  status = Flash_IsReady();
  Flash_PID_SetDefault(pid_table);
  memcpy(cached_pid, pid_table, sizeof(cached_pid));
  cache_valid = 1U;

  if (status != HAL_OK)
  {
    return status;
  }

  return Flash_PID_Save(pid_table);
}

HAL_StatusTypeDef Flash_PID_SaveIfChanged(const PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT])
{
  if (pid_table == NULL)
  {
    return HAL_ERROR;
  }

  if (fram_available == 0U)
  {
    return HAL_ERROR;
  }

  if ((cache_valid == 0U) || (Flash_PID_IsDifferent(pid_table, cached_pid) != 0U))
  {
    return Flash_PID_Save(pid_table);
  }

  return HAL_OK;
}

static uint32_t Flash_CalcChecksum(const uint8_t *data, uint32_t len)
{
  uint32_t hash = 2166136261UL;

  for (uint32_t i = 0U; i < len; i++)
  {
    hash ^= data[i];
    hash *= 16777619UL;
  }

  return hash;
}

static uint8_t Flash_PID_IsDifferent(const PID_Params_t a[FLASH_PID_MOTOR_COUNT], const PID_Params_t b[FLASH_PID_MOTOR_COUNT])
{
  return (memcmp(a, b, sizeof(PID_Params_t) * FLASH_PID_MOTOR_COUNT) != 0) ? 1U : 0U;
}

static void Flash_PID_SetDefault(PID_Params_t pid_table[FLASH_PID_MOTOR_COUNT])
{
  for (uint8_t i = 0U; i < FLASH_PID_MOTOR_COUNT; i++)
  {
    pid_table[i].kp = 0.0f;
    pid_table[i].ki = 0.1f;
    pid_table[i].kd = 0.0f;
  }
}

