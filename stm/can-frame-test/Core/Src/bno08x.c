#include "bno08x.h"
#include <math.h>
#include <string.h>

/* BNO08x datasheet section 1.3; SH-2 reference manual sections 6.5 and 7.2. */
#define CHANNEL_EXECUTABLE  1U
#define CHANNEL_CONTROL     2U
#define CHANNEL_REPORTS     3U
#define REPORT_ACCEL        0x01U
#define REPORT_GYRO         0x02U
#define REPORT_MAG          0x03U
#define REPORT_ROTATION     0x05U
#define REPORT_PRODUCT_ID   0xF8U
#define REPORT_PRODUCT_REQ  0xF9U
#define REPORT_SET_FEATURE  0xFDU
#define I2C_TIMEOUT_MS      25U
#define REPORT_TIMEOUT_MS   1000U
#define PACKET_SIZE         512U
#define RAD_TO_DEG          57.2957795131f

static int16_t le16(const uint8_t *p)
{
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static HAL_StatusTypeDef send_packet(bno08x_t *dev, uint8_t channel,
                                    const uint8_t *data, uint8_t len)
{
  uint8_t packet[21] = {len + 4U, 0, channel, dev->sequence[channel]++};
  memcpy(packet + 4, data, len);
  return HAL_I2C_Master_Transmit(dev->hi2c, dev->addr, packet, len + 4U,
                                 I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef receive_packet(bno08x_t *dev, uint8_t *channel,
                                       uint8_t *data, uint16_t *len)
{
  uint8_t buf[132];
  *len = 0;
  HAL_StatusTypeDef st = HAL_I2C_Master_Receive(dev->hi2c, dev->addr, buf, 4,
                                              I2C_TIMEOUT_MS);
  if (st != HAL_OK) {
    /* An idle BNO08x may NACK instead of returning an empty header. */
    if (st == HAL_ERROR && HAL_I2C_GetError(dev->hi2c) == HAL_I2C_ERROR_AF)
      return HAL_BUSY;
    return st;
  }
  uint16_t total = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
  if (total == 0xFFFFU) return HAL_ERROR; /* reserved/failed-peripheral header */
  total &= 0x7FFFU;
  if (total == 0) return HAL_BUSY;
  if (total < 4) return HAL_ERROR;
  *channel = buf[2];

  /* Each I2C read repeats the four-byte SHTP header, including after a
     header-only read. Consume oversized advertisements without copying them. */
  uint16_t remaining = total - 4U;
  uint16_t offset = 0;
  while (remaining) {
    uint16_t n = remaining > 128U ? 128U : remaining;
    st = HAL_I2C_Master_Receive(dev->hi2c, dev->addr, buf, n + 4U, I2C_TIMEOUT_MS);
    if (st != HAL_OK) return st;
    if (total - 4U <= PACKET_SIZE) memcpy(data + offset, buf + 4, n);
    offset += n;
    remaining -= n;
  }
  if (offset <= PACKET_SIZE) *len = offset;
  return HAL_OK;
}

static HAL_StatusTypeDef wait_for(bno08x_t *dev, uint8_t wanted_channel,
                                 uint8_t report, uint16_t min_len)
{
  uint32_t start = HAL_GetTick();
  uint8_t data[PACKET_SIZE], channel;
  uint16_t len;
  while (HAL_GetTick() - start < REPORT_TIMEOUT_MS) {
    HAL_StatusTypeDef st = receive_packet(dev, &channel, data, &len);
    if (st == HAL_OK && len >= min_len && channel == wanted_channel && data[0] == report)
      return HAL_OK;
    if (st != HAL_OK && st != HAL_BUSY) return st;
    HAL_Delay(1);
  }
  return HAL_TIMEOUT;
}

HAL_StatusTypeDef bno08x_init(bno08x_t *dev, I2C_HandleTypeDef *hi2c,
                            uint8_t addr7, uint16_t period_ms)
{
  memset(dev, 0, sizeof *dev);
  dev->hi2c = hi2c;
  dev->addr = (uint16_t)(addr7 << 1);
  if (HAL_I2C_IsDeviceReady(hi2c, dev->addr, 1, I2C_TIMEOUT_MS) != HAL_OK)
    return HAL_ERROR;

  const uint8_t reset = 1;
  if (send_packet(dev, CHANNEL_EXECUTABLE, &reset, 1) != HAL_OK) return HAL_ERROR;
  HAL_Delay(100);
  if (wait_for(dev, CHANNEL_EXECUTABLE, 1, 1) != HAL_OK) return HAL_ERROR;
  HAL_Delay(50);

  const uint8_t product_request[2] = {REPORT_PRODUCT_REQ, 0};
  if (send_packet(dev, CHANNEL_CONTROL, product_request, sizeof product_request) != HAL_OK)
    return HAL_ERROR;
  if (wait_for(dev, CHANNEL_CONTROL, REPORT_PRODUCT_ID, 14) != HAL_OK) return HAL_ERROR;

  const uint8_t reports[] = {REPORT_ACCEL, REPORT_GYRO, REPORT_MAG, REPORT_ROTATION};
  uint32_t interval_us = (uint32_t)period_ms * 1000U;
  for (unsigned i = 0; i < sizeof reports; i++) {
    uint8_t feature[17] = {REPORT_SET_FEATURE, reports[i]};
    for (unsigned j = 0; j < 4; j++) feature[5 + j] = (uint8_t)(interval_us >> (8 * j));
    if (send_packet(dev, CHANNEL_CONTROL, feature, sizeof feature) != HAL_OK) return HAL_ERROR;
  }
  for (unsigned i = 0; i < 4; i++) dev->last_report[i] = HAL_GetTick();
  return HAL_OK;
}

static int16_t counts(float value)
{
  if (value > INT16_MAX) return INT16_MAX;
  if (value < INT16_MIN) return INT16_MIN;
  return (int16_t)lroundf(value);
}

static bool quaternion_to_euler(const uint8_t *p, int16_t euler[3])
{
  float x = le16(p) / 16384.0f, y = le16(p + 2) / 16384.0f;
  float z = le16(p + 4) / 16384.0f, w = le16(p + 6) / 16384.0f;
  float norm = x*x + y*y + z*z + w*w;
  if (norm == 0.0f) return false;

  /* Match the old Windows Euler layout: clockwise heading about Z,
     roll about Y (+/-90), pitch about X (+/-180). */
  float heading = -atan2f(2.0f * (w*z + x*y), norm - 2.0f * (y*y + z*z)) * RAD_TO_DEG;
  if (heading < 0.0f) heading += 360.0f;
  float sin_roll = 2.0f * (w*y - z*x) / norm;
  sin_roll = fmaxf(-1.0f, fminf(1.0f, sin_roll));
  euler[0] = (int16_t)(counts(heading * 16.0f) % 5760);
  euler[1] = counts(-asinf(sin_roll) * RAD_TO_DEG * 16.0f);
  euler[2] = counts(atan2f(2.0f * (w*x + y*z), norm - 2.0f * (x*x + y*y)) * RAD_TO_DEG * 16.0f);
  return true;
}

static HAL_StatusTypeDef parse_reports(bno08x_t *dev, const uint8_t *p, uint16_t len,
                                      bool *updated)
{
  while (len) {
    uint8_t id = p[0];
    if (id == 0xFBU || id == 0xFAU) { /* base timestamp / timestamp rebase */
      if (len < 5) return HAL_ERROR;
      p += 5;
      len -= 5;
      continue;
    }
    unsigned index, shift;
    switch (id) {
      case REPORT_ACCEL:    index = 0; shift = 2; break;
      case REPORT_GYRO:     index = 1; shift = 4; break;
      case REPORT_MAG:      index = 2; shift = 0; break;
      case REPORT_ROTATION: index = 3; shift = 6; break;
      default: return HAL_ERROR; /* don't guess the length of unknown reports */
    }
    uint16_t size = id == REPORT_ROTATION ? 14U : 10U;
    if (len < size) return HAL_ERROR;
    if (id == REPORT_ROTATION) {
      if (!quaternion_to_euler(p + 4, dev->data.euler)) return HAL_ERROR;
    } else {
      for (unsigned i = 0; i < 3; i++) {
        int16_t raw = le16(p + 4 + 2*i);
        if (id == REPORT_ACCEL) dev->data.accel[i] = counts(raw * (100.0f / 256.0f));
        if (id == REPORT_GYRO)  dev->data.gyro[i] = counts(raw * (RAD_TO_DEG * 16.0f / 512.0f));
        if (id == REPORT_MAG)   dev->data.mag[i] = raw;
      }
    }
    dev->data.calib = (dev->data.calib & ~(3U << shift)) | ((p[2] & 3U) << shift);
    dev->seen |= (uint8_t)(1U << index);
    dev->last_report[index] = HAL_GetTick();
    *updated = true;
    p += size;
    len -= size;
  }
  return HAL_OK;
}

HAL_StatusTypeDef bno08x_read(bno08x_t *dev, bno08x_data_t *out)
{
  if (dev->reset) return HAL_ERROR;
  uint8_t data[PACKET_SIZE], channel;
  uint16_t len;
  bool updated = false;
  /* Bound the work per service call while allowing separate/batched reports. */
  for (unsigned i = 0; i < 8; i++) {
    HAL_StatusTypeDef st = receive_packet(dev, &channel, data, &len);
    if (st == HAL_BUSY) break;
    if (st != HAL_OK) return st;
    if (!len) continue;
    if (channel == CHANNEL_EXECUTABLE && data[0] == 1) {
      dev->reset = true;
      return HAL_ERROR;
    }
    if (channel == CHANNEL_REPORTS) {
      st = parse_reports(dev, data, len, &updated);
      if (st != HAL_OK) return st;
    }
  }
  for (unsigned i = 0; i < 4; i++) {
    if (HAL_GetTick() - dev->last_report[i] >= REPORT_TIMEOUT_MS) return HAL_TIMEOUT;
  }
  if (dev->seen != 0x0FU || !updated) return HAL_BUSY;
  *out = dev->data;
  return HAL_OK;
}
