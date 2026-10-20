/**
  ******************************************************************************
  * @file    lcd_port.h
  * @brief   Glue between LVGL's built-in ILI9341 driver and the STM32 HAL.
  *
  *          LVGL owns the controller (init sequence, rotation, flushing); this
  *          module only supplies the two bus callbacks LVGL asks for and the
  *          hardware reset pulse.
  *
  *          Each panel has its own SPI bus (4-wire, mode 0, 8-bit, MSB first,
  *          write only) and its own CS / DC / RST pins, described by an
  *          lcd_port_t. Up to LCD_PORT_MAX_DISPLAYS panels are supported.
  ******************************************************************************
  */
#ifndef LCD_PORT_H
#define LCD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "lvgl.h"

/* Native (portrait) resolution of the panel. */
#define LCD_HOR_RES   240U
#define LCD_VER_RES   320U

/* Each panel gets its own 15 KB draw buffer in RAM. */
#define LCD_PORT_MAX_DISPLAYS   2U

/* Orientation and colour order passed to lv_ili9341_create().
   LVGL's ILI9341 init sequence sets SS=1 in Display Function Control (0xB6),
   which already mirrors the source outputs, so portrait needs no MADCTL MX
   bit. (The old hand-written driver used SS=0 plus MX, which looks the same.)
     Image mirrored left-right  -> add LV_LCD_FLAG_MIRROR_X
     Image upside down          -> add LV_LCD_FLAG_MIRROR_Y
     Red and blue swapped       -> remove LV_LCD_FLAG_BGR */
#ifndef LCD_FLAGS
#define LCD_FLAGS     (LV_LCD_FLAG_BGR)
#endif

/* One panel: its SPI bus and control pins. Must stay valid for as long as the
   display exists (make it static or const). */
typedef struct
{
  SPI_HandleTypeDef *hspi;
  GPIO_TypeDef *cs_port;
  uint16_t      cs_pin;
  GPIO_TypeDef *dc_port;
  uint16_t      dc_pin;
  GPIO_TypeDef *rst_port;
  uint16_t      rst_pin;
} lcd_port_t;

/* Pulse the panel's reset line, create an LVGL ILI9341 display on it and give
   it a partial-render draw buffer. lv_init(), lv_tick_set_cb() and
   lv_delay_set_cb() must have been called first: the init sequence uses
   lv_delay_ms(). The first display created becomes LVGL's default display.
   Returns NULL if LVGL runs out of memory or all LCD_PORT_MAX_DISPLAYS draw
   buffers are taken. */
lv_display_t *lcd_port_init(const lcd_port_t *port);

#ifdef __cplusplus
}
#endif

#endif /* LCD_PORT_H */
