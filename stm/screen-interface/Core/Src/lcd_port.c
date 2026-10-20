/**
  ******************************************************************************
  * @file    lcd_port.c
  * @brief   Glue between LVGL's built-in ILI9341 driver and the STM32 HAL.
  ******************************************************************************
  */
#include "lcd_port.h"

/* LVGL renders 1/10 of the screen at a time: 32 lines of RGB565, 15 KB. */
#define DRAW_BUF_SIZE       (LCD_HOR_RES * (LCD_VER_RES / 10U) * 2U)

/* Reset pulse needs >= 10 us; the controller needs 120 ms after reset before
   it accepts commands. */
#define RESET_PULSE_MS      10U
#define RESET_RECOVER_MS    120U

/* A full draw buffer takes ~12 ms at 10.6 MHz and ~0.2 s at the slowest
   prescaler (/256). */
#define SPI_TIMEOUT_MS      1000U

/* HAL transfer sizes are uint16_t. */
#define SPI_MAX_CHUNK       0xFFFFU

static uint8_t s_draw_buf[LCD_PORT_MAX_DISPLAYS][DRAW_BUF_SIZE] __attribute__((aligned(4)));
static uint32_t s_display_count;

/* lv_ili9341_create() sends the init sequence before it returns, i.e. before
   the port can be attached to the display as user data. The callbacks use
   this port until then. */
static const lcd_port_t *s_port_being_created;

static const lcd_port_t *get_port(lv_display_t *disp)
{
  const lcd_port_t *port = lv_display_get_user_data(disp);

  return (port != NULL) ? port : s_port_being_created;
}

/* Blocking write, split to fit the HAL's 16-bit length. A failed transfer
   only corrupts the area being drawn, which LVGL redraws on the next change,
   so errors are not propagated. */
static void spi_write(const lcd_port_t *port, const uint8_t *data, size_t len)
{
  while (len > 0U)
  {
    uint16_t n = (len > SPI_MAX_CHUNK) ? SPI_MAX_CHUNK : (uint16_t)len;

    (void)HAL_SPI_Transmit(port->hspi, data, n, SPI_TIMEOUT_MS);
    data += n;
    len -= n;
  }
}

/* One CS frame: command bytes with DC low, then parameter bytes with DC high.
   HAL_SPI_Transmit() returns only once the bus is idle, so DC and CS can be
   changed straight after it. */
static void lcd_write(const lcd_port_t *port, const uint8_t *cmd, size_t cmd_size,
                      const uint8_t *param, size_t param_size)
{
  HAL_GPIO_WritePin(port->cs_port, port->cs_pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(port->dc_port, port->dc_pin, GPIO_PIN_RESET);
  spi_write(port, cmd, cmd_size);
  if (param_size > 0U)
  {
    HAL_GPIO_WritePin(port->dc_port, port->dc_pin, GPIO_PIN_SET);
    spi_write(port, param, param_size);
  }
  HAL_GPIO_WritePin(port->cs_port, port->cs_pin, GPIO_PIN_SET);
}

/* LVGL callback for commands (init sequence, address window, rotation). */
static void lcd_send_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                         const uint8_t *param, size_t param_size)
{
  lcd_write(get_port(disp), cmd, cmd_size, param, param_size);
}

/* LVGL callback for pixel data (RAMWR + the rendered area). The transfer is
   blocking, so the buffer can be handed back to LVGL as soon as it returns.
   With DMA, lv_display_flush_ready() would move to the transfer-complete
   callback instead. */
static void lcd_send_color(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size,
                           uint8_t *param, size_t param_size)
{
  lcd_write(get_port(disp), cmd, cmd_size, param, param_size);
  lv_display_flush_ready(disp);
}

lv_display_t *lcd_port_init(const lcd_port_t *port)
{
  lv_display_t *disp;
  uint8_t *buf;

  if (s_display_count >= LCD_PORT_MAX_DISPLAYS)
  {
    return NULL;
  }

  /* Hardware reset. LVGL's init sequence follows it with a software reset,
     sleep out and display on. */
  HAL_GPIO_WritePin(port->cs_port, port->cs_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(port->dc_port, port->dc_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(port->rst_port, port->rst_pin, GPIO_PIN_RESET);
  HAL_Delay(RESET_PULSE_MS);
  HAL_GPIO_WritePin(port->rst_port, port->rst_pin, GPIO_PIN_SET);
  HAL_Delay(RESET_RECOVER_MS);

  s_port_being_created = port;
  disp = lv_ili9341_create(LCD_HOR_RES, LCD_VER_RES, LCD_FLAGS, lcd_send_cmd, lcd_send_color);
  s_port_being_created = NULL;
  if (disp == NULL)
  {
    return NULL;
  }
  lv_display_set_user_data(disp, (void *)port);

  /* LVGL normally renders RGB565 little-endian, but the ILI9341 wants the
     high byte first on an 8-bit bus. Rendering straight into the swapped
     format saves a byte swap on every flush. Must be set before the buffers. */
  buf = s_draw_buf[s_display_count++];
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(disp, buf, NULL, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  return disp;
}
