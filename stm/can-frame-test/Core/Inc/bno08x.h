#ifndef BNO08X_H
#define BNO08X_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

#define BNO08X_ADDR_LOW   0x4AU
#define BNO08X_ADDR_HIGH  0x4BU

/* Existing CAN units: accel 100 LSB/(m/s^2), gyro 16 LSB/(deg/s),
   mag 16 LSB/uT, Euler 16 LSB/deg. */
typedef struct {
  int16_t accel[3];
  int16_t gyro[3];
  int16_t mag[3];
  int16_t euler[3];          /* heading [0,360), roll about Y, pitch about X */
  uint8_t calib;            /* 2-bit accuracy: rotation, gyro, accel, mag */
} bno08x_data_t;

typedef struct {
  I2C_HandleTypeDef *hi2c;
  uint16_t addr;            /* HAL address: 7-bit << 1 */
  uint8_t sequence[3];
  uint8_t seen;
  bool reset;
  uint32_t last_report[4];
  bno08x_data_t data;
} bno08x_t;

/* I2C/SHTP mode, polling (no INT pin required). Soft-resets, verifies SH-2,
   and enables accel, gyro, mag and rotation vector at period_ms intervals. */
HAL_StatusTypeDef bno08x_init(bno08x_t *dev, I2C_HandleTypeDef *hi2c,
                            uint8_t addr7, uint16_t period_ms);

/* Drains pending reports and returns the latest values once all four outputs
   have arrived. HAL_BUSY means no new complete data; HAL_TIMEOUT means a
   report stream has stalled for a second. Other errors require re-init. */
HAL_StatusTypeDef bno08x_read(bno08x_t *dev, bno08x_data_t *out);

#endif
