#include "bno055.h"

/* Register map (page 0) */
#define REG_CHIP_ID      0x00U
#define REG_ACC_DATA_X   0x08U  /* start of accel, mag, gyro, euler block */
#define REG_CALIB_STAT   0x35U
#define REG_UNIT_SEL     0x3BU
#define REG_OPR_MODE     0x3DU
#define REG_PWR_MODE     0x3EU
#define REG_SYS_TRIGGER  0x3FU
#define REG_PAGE_ID      0x07U

#define CHIP_ID_VALUE    0xA0U
#define MODE_CONFIG      0x00U
#define MODE_NDOF        0x0CU
#define PWR_NORMAL       0x00U
#define SYS_RST          0x20U

#define I2C_TIMEOUT_MS   10U
#define BOOT_TIMEOUT_MS  1000U  /* datasheet: 650 ms typical after reset */

static HAL_StatusTypeDef write_reg(bno055_t *dev, uint8_t reg, uint8_t val)
{
  return HAL_I2C_Mem_Write(dev->hi2c, dev->addr, reg, I2C_MEMADD_SIZE_8BIT,
                           &val, 1, I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef read_regs(bno055_t *dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
  return HAL_I2C_Mem_Read(dev->hi2c, dev->addr, reg, I2C_MEMADD_SIZE_8BIT,
                          buf, len, I2C_TIMEOUT_MS);
}

/* The chip NACKs while it boots, so poll the ID register until it answers. */
static HAL_StatusTypeDef wait_for_chip_id(bno055_t *dev)
{
  uint32_t start = HAL_GetTick();
  uint8_t id = 0;
  while ((HAL_GetTick() - start) < BOOT_TIMEOUT_MS) {
    if (read_regs(dev, REG_CHIP_ID, &id, 1) == HAL_OK && id == CHIP_ID_VALUE)
      return HAL_OK;
    HAL_Delay(10);
  }
  return HAL_TIMEOUT;
}

static int16_t le16(const uint8_t *p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

HAL_StatusTypeDef bno055_init(bno055_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
  dev->hi2c = hi2c;
  dev->addr = (uint16_t)(addr7 << 1);

  if (wait_for_chip_id(dev) != HAL_OK) return HAL_ERROR;

  /* Config mode, then reset so we start from a known state even if only the
     MCU was reset. Switching into config mode takes up to 19 ms. */
  if (write_reg(dev, REG_OPR_MODE, MODE_CONFIG) != HAL_OK) return HAL_ERROR;
  HAL_Delay(25);
  (void)write_reg(dev, REG_SYS_TRIGGER, SYS_RST);  /* chip may NACK as it resets */
  HAL_Delay(50);
  if (wait_for_chip_id(dev) != HAL_OK) return HAL_ERROR;

  if (write_reg(dev, REG_PWR_MODE, PWR_NORMAL) != HAL_OK) return HAL_ERROR;
  HAL_Delay(10);
  if (write_reg(dev, REG_PAGE_ID, 0) != HAL_OK) return HAL_ERROR;
  /* m/s^2, dps, degrees, Celsius, Windows orientation */
  if (write_reg(dev, REG_UNIT_SEL, 0x00) != HAL_OK) return HAL_ERROR;
  /* Internal oscillator */
  if (write_reg(dev, REG_SYS_TRIGGER, 0x00) != HAL_OK) return HAL_ERROR;
  HAL_Delay(10);

  /* Config -> fusion mode takes 7 ms. */
  if (write_reg(dev, REG_OPR_MODE, MODE_NDOF) != HAL_OK) return HAL_ERROR;
  HAL_Delay(20);

  return HAL_OK;
}

HAL_StatusTypeDef bno055_read(bno055_t *dev, bno055_data_t *out)
{
  /* 0x08-0x1F: accel(6) mag(6) gyro(6) euler(6) */
  uint8_t buf[24];
  HAL_StatusTypeDef st = read_regs(dev, REG_ACC_DATA_X, buf, sizeof buf);
  if (st != HAL_OK) return st;

  st = read_regs(dev, REG_CALIB_STAT, &out->calib, 1);
  if (st != HAL_OK) return st;

  for (int i = 0; i < 3; i++) {
    out->accel[i] = le16(&buf[0  + 2 * i]);
    out->mag[i]   = le16(&buf[6  + 2 * i]);
    out->gyro[i]  = le16(&buf[12 + 2 * i]);
    out->euler[i] = le16(&buf[18 + 2 * i]);
  }
  return HAL_OK;
}
