/**
 * Minimal BNO055 driver (I2C, blocking HAL calls).
 *
 * Runs the sensor in NDOF fusion mode and reads accel, gyro, mag, Euler
 * angles and calibration status. Values are left as the chip's raw int16
 * counts; scale factors with UNIT_SEL = 0x00:
 *   accel  : 1 m/s^2 = 100 LSB
 *   gyro   : 1 dps   = 16 LSB
 *   mag    : 1 uT    = 16 LSB
 *   euler  : 1 deg   = 16 LSB  (heading, roll, pitch)
 */
#ifndef BNO055_H
#define BNO055_H

#include "main.h"
#include <stdint.h>

#define BNO055_ADDR_LOW   0x28U  /* ADR pin low (default on most breakouts) */
#define BNO055_ADDR_HIGH  0x29U  /* ADR pin high */

typedef struct {
  I2C_HandleTypeDef *hi2c;
  uint16_t addr;              /* HAL-style 8-bit address (7-bit << 1) */
} bno055_t;

typedef struct {
  int16_t accel[3];           /* x, y, z */
  int16_t gyro[3];            /* x, y, z */
  int16_t mag[3];             /* x, y, z */
  int16_t euler[3];           /* heading, roll, pitch */
  uint8_t calib;              /* CALIB_STAT: [7:6] sys, [5:4] gyro, [3:2] accel, [1:0] mag; 3 = fully calibrated */
} bno055_data_t;

/* Resets the chip and puts it in NDOF mode. Blocks for roughly 0.7-1.5 s. */
HAL_StatusTypeDef bno055_init(bno055_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr7);

/* Reads one sample of all outputs (two I2C transfers, ~3 ms at 100 kHz). */
HAL_StatusTypeDef bno055_read(bno055_t *dev, bno055_data_t *out);

#endif /* BNO055_H */
