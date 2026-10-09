/**
  ******************************************************************************
  * @file    ili9341.h
  * @brief   Small ILI9341 240x320 SPI TFT driver (STM32 HAL, blocking).
  *
  *          Bus: 4-wire SPI, mode 0, 8-bit, MSB first, software CS, write only.
  *          CS / DC / RST pins come from the CubeMX user labels TFT_CS, TFT_DC
  *          and TFT_RST (see main.h); override the macros below to use others.
  ******************************************************************************
  */
#ifndef ILI9341_H
#define ILI9341_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* ---- Configuration --------------------------------------------------------*/

/* SPI clock = PCLK2 / prescaler. PCLK2 is 170 MHz in this project:
     SPI_BAUDRATEPRESCALER_16 -> 10.6 MHz (default; the datasheet write limit)
     SPI_BAUDRATEPRESCALER_8  -> 21.3 MHz (beyond the datasheet, usually works)
     SPI_BAUDRATEPRESCALER_4  -> 42.5 MHz (short wires only, test it)
   The driver applies this every time it takes the bus, so a second device on
   SPI1 (e.g. XPT2046 touch at ~2 MHz) can use its own prescaler. */
#ifndef ILI9341_SPI_PRESCALER
#define ILI9341_SPI_PRESCALER   SPI_BAUDRATEPRESCALER_16
#endif

/* 1 = send large pixel blocks with HAL_SPI_Transmit_DMA, 0 = polling only.
   Needs in CubeMX: SPI1 > DMA Settings > Add SPI1_TX (DMA1 channel, Normal
   mode, Memory increment, Byte/Byte) with the DMA channel global interrupt
   enabled (CubeMX forces it on). Buffers passed to ili9341_write_pixels() must
   then stay valid until the call returns, which it does only after the
   transfer has finished. */
#ifndef ILI9341_USE_DMA
#define ILI9341_USE_DMA         0
#endif

/* Blocks shorter than this many bytes are always sent by polling. */
#ifndef ILI9341_DMA_MIN_BYTES
#define ILI9341_DMA_MIN_BYTES   64U
#endif

/* 1 = panel has BGR colour filter order (almost all ILI9341 modules).
   Set to 0 if red and blue are swapped. */
#ifndef ILI9341_PANEL_BGR
#define ILI9341_PANEL_BGR       1
#endif

/* Size in bytes of the static buffer used for fills and text (even number). */
#ifndef ILI9341_BUF_SIZE
#define ILI9341_BUF_SIZE        512U
#endif

#ifndef ILI9341_CS_GPIO_Port
#define ILI9341_CS_GPIO_Port    TFT_CS_GPIO_Port
#define ILI9341_CS_Pin          TFT_CS_Pin
#endif
#ifndef ILI9341_DC_GPIO_Port
#define ILI9341_DC_GPIO_Port    TFT_DC_GPIO_Port
#define ILI9341_DC_Pin          TFT_DC_Pin
#endif
#ifndef ILI9341_RST_GPIO_Port
#define ILI9341_RST_GPIO_Port   TFT_RST_GPIO_Port
#define ILI9341_RST_Pin         TFT_RST_Pin
#endif

/* ---- Geometry and colours -------------------------------------------------*/

#define ILI9341_TFTWIDTH        240U   /* native (portrait) width */
#define ILI9341_TFTHEIGHT       320U   /* native (portrait) height */

/* 8-bit R, G, B -> RGB565 */
#define RGB565(r, g, b) \
  ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | (((b) & 0xFFU) >> 3)))

#define ILI9341_BLACK           0x0000U
#define ILI9341_WHITE           0xFFFFU
#define ILI9341_RED             0xF800U
#define ILI9341_GREEN           0x07E0U
#define ILI9341_BLUE            0x001FU
#define ILI9341_YELLOW          0xFFE0U
#define ILI9341_CYAN            0x07FFU
#define ILI9341_MAGENTA         0xF81FU
#define ILI9341_ORANGE          0xFD20U
#define ILI9341_GRAY            0x8410U

/* ---- API ------------------------------------------------------------------*/

/* Hardware reset + init sequence. Leaves the display on, in rotation 0
   (portrait). Blocks for about 250 ms. Returns HAL_OK or the SPI error. */
HAL_StatusTypeDef ili9341_init(SPI_HandleTypeDef *hspi);

/* First SPI error since init (HAL_OK if none). Drawing calls do not return
   a status; check this if the screen misbehaves. */
HAL_StatusTypeDef ili9341_status(void);

/* r = 0..3: 0 portrait, 1 landscape, 2 portrait flipped, 3 landscape flipped. */
void ili9341_set_rotation(uint8_t r);
uint16_t ili9341_width(void);
uint16_t ili9341_height(void);

/* Set the drawing window (inclusive corners) and start a RAM write. Follow
   with ili9341_write_pixels() to stream pixels left-to-right, top-to-bottom. */
void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/* Stream raw pixel bytes (RGB565, high byte first) into the current window.
   Any length; may be called repeatedly. This is the hook for a framebuffer
   or LVGL flush callback. */
void ili9341_write_pixels(const uint8_t *data, uint32_t len);

void ili9341_fill_screen(uint16_t color);
void ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ili9341_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ili9341_draw_pixel(int16_t x, int16_t y, uint16_t color);

/* Text uses the 5x7 font in a 6x8 cell, multiplied by scale (>= 1). The cell
   background is painted with bg, so redrawing text in place needs no erase.
   draw_string handles '\n' and returns the x position after the last char. */
void ili9341_draw_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
int16_t ili9341_draw_string(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif /* ILI9341_H */
