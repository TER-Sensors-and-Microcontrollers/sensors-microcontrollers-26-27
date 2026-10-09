/**
  ******************************************************************************
  * @file    ili9341.c
  * @brief   Small ILI9341 240x320 SPI TFT driver (STM32 HAL, blocking).
  ******************************************************************************
  */
#include "ili9341.h"
#include "font.h"
#include <stddef.h>

/* ---- ILI9341 commands -----------------------------------------------------*/
#define CMD_SLPOUT      0x11U  /* Sleep out */
#define CMD_GAMMASET    0x26U  /* Gamma curve select */
#define CMD_DISPON      0x29U  /* Display on */
#define CMD_CASET       0x2AU  /* Column address set */
#define CMD_PASET       0x2BU  /* Page (row) address set */
#define CMD_RAMWR       0x2CU  /* Memory write */
#define CMD_MADCTL      0x36U  /* Memory access control */
#define CMD_VSCRSADD    0x37U  /* Vertical scroll start address */
#define CMD_PIXFMT      0x3AU  /* Pixel format */
#define CMD_FRMCTR1     0xB1U  /* Frame rate control, normal mode */
#define CMD_DFUNCTR     0xB6U  /* Display function control */
#define CMD_PWCTR1      0xC0U  /* Power control 1 */
#define CMD_PWCTR2      0xC1U  /* Power control 2 */
#define CMD_VMCTR1      0xC5U  /* VCOM control 1 */
#define CMD_VMCTR2      0xC7U  /* VCOM control 2 */
#define CMD_PWCTRA      0xCBU  /* Power control A */
#define CMD_PWCTRB      0xCFU  /* Power control B */
#define CMD_GMCTRP1     0xE0U  /* Positive gamma correction */
#define CMD_GMCTRN1     0xE1U  /* Negative gamma correction */
#define CMD_DTCA        0xE8U  /* Driver timing control A */
#define CMD_DTCB        0xEAU  /* Driver timing control B */
#define CMD_PWRSEQ      0xEDU  /* Power on sequence control */
#define CMD_3GAMMA      0xF2U  /* 3-gamma function enable */
#define CMD_PUMPRATIO   0xF7U  /* Pump ratio control */

/* MADCTL bits */
#define MADCTL_MY       0x80U  /* row address order */
#define MADCTL_MX       0x40U  /* column address order */
#define MADCTL_MV       0x20U  /* row/column exchange */
#define MADCTL_BGR      0x08U  /* BGR colour filter panel */

#if ILI9341_PANEL_BGR
#define MADCTL_ORDER    MADCTL_BGR
#else
#define MADCTL_ORDER    0x00U
#endif

/* Timing (ms). Reset pulse needs >= 10 us; the controller needs 120 ms after
   reset before Sleep Out, and 120 ms after Sleep Out for the supplies to
   settle before the display is turned on. */
#define RESET_PULSE_MS      10U
#define RESET_RECOVER_MS    120U
#define SLPOUT_MS           120U

/* Per-chunk SPI timeout: a full 64 KB chunk takes ~50 ms at 10 MHz and
   ~0.8 s at the slowest prescaler (/256). */
#define SPI_TIMEOUT_MS      2000U

/* HAL transfer sizes are uint16_t; keep chunks to a whole number of pixels. */
#define SPI_MAX_CHUNK       0xFFFEU

_Static_assert((ILI9341_BUF_SIZE >= 2U) && ((ILI9341_BUF_SIZE % 2U) == 0U),
               "ILI9341_BUF_SIZE must be a non-zero even number");

typedef struct
{
  uint8_t cmd;
  uint8_t len;
  uint8_t data[15];
} init_cmd_t;

static const init_cmd_t init_cmds[] = {
  /* Vendor power-up settings recommended for this panel type */
  {CMD_PWCTRB,    3, {0x00, 0xC1, 0x30}},             /* PCEQ on, ESD protection (DC_ena) on */
  {CMD_PWRSEQ,    4, {0x64, 0x03, 0x12, 0x81}},       /* soft-start + DDVDH enhance mode */
  {CMD_DTCA,      3, {0x85, 0x00, 0x78}},             /* gate non-overlap / EQ / precharge timing */
  {CMD_PWCTRA,    5, {0x39, 0x2C, 0x00, 0x34, 0x02}}, /* Vcore 1.6 V, DDVDH 5.6 V */
  {CMD_PUMPRATIO, 1, {0x20}},                         /* DDVDH = 2 x VCI */
  {CMD_DTCB,      2, {0x00, 0x00}},                   /* gate driver timing: no delay */

  {CMD_PWCTR1,    1, {0x23}},                         /* VRH: GVDD = 4.60 V */
  {CMD_PWCTR2,    1, {0x10}},                         /* step-up: VGH = VCI x 7, VGL = -VCI x 4 */
  {CMD_VMCTR1,    2, {0x3E, 0x28}},                   /* VCOMH = 4.25 V, VCOML = -1.50 V */
  {CMD_VMCTR2,    1, {0x86}},                         /* VCOM offset enabled, -58 steps */

  {CMD_MADCTL,    1, {MADCTL_MX | MADCTL_ORDER}},     /* rotation 0 (portrait) */
  {CMD_VSCRSADD,  2, {0x00, 0x00}},                   /* no vertical scroll offset */
  {CMD_PIXFMT,    1, {0x55}},                         /* 16 bit/pixel (RGB565) on both interfaces */
  {CMD_FRMCTR1,   2, {0x00, 0x18}},                   /* fosc / 1, 24 clocks per line -> 79 Hz */
  {CMD_DFUNCTR,   3, {0x08, 0x82, 0x27}},             /* normally-white panel, 320 lines (0x27) */

  {CMD_3GAMMA,    1, {0x00}},                         /* 3-gamma off */
  {CMD_GAMMASET,  1, {0x01}},                         /* gamma curve 1 (G2.2) */
  {CMD_GMCTRP1,  15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                      0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}},
  {CMD_GMCTRN1,  15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                      0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}},
};

static SPI_HandleTypeDef *s_hspi;
static HAL_StatusTypeDef s_status = HAL_OK;
static uint16_t s_width = ILI9341_TFTWIDTH;
static uint16_t s_height = ILI9341_TFTHEIGHT;
static uint8_t s_buf[ILI9341_BUF_SIZE];

/* ---- Pin and bus helpers --------------------------------------------------*/

static inline void cs_low(void)
{
  HAL_GPIO_WritePin(ILI9341_CS_GPIO_Port, ILI9341_CS_Pin, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
  HAL_GPIO_WritePin(ILI9341_CS_GPIO_Port, ILI9341_CS_Pin, GPIO_PIN_SET);
}

static inline void dc_command(void)
{
  HAL_GPIO_WritePin(ILI9341_DC_GPIO_Port, ILI9341_DC_Pin, GPIO_PIN_RESET);
}

static inline void dc_data(void)
{
  HAL_GPIO_WritePin(ILI9341_DC_GPIO_Port, ILI9341_DC_Pin, GPIO_PIN_SET);
}

/* Take the bus for the display: make sure SPI runs at the display's speed
   (another device on the same bus may have changed it), then assert CS. */
static void bus_begin(void)
{
  SPI_TypeDef *spi = s_hspi->Instance;

  if ((spi->CR1 & SPI_CR1_BR_Msk) != ILI9341_SPI_PRESCALER)
  {
    /* BR must only be changed while the peripheral is disabled; the HAL
       re-enables it on the next transfer. */
    __HAL_SPI_DISABLE(s_hspi);
    MODIFY_REG(spi->CR1, SPI_CR1_BR_Msk, ILI9341_SPI_PRESCALER);
    s_hspi->Init.BaudRatePrescaler = ILI9341_SPI_PRESCALER;
  }
  cs_low();
}

static void bus_end(void)
{
  cs_high();
}

/* Send bytes with CS/DC already set up; splits anything over the HAL's
   16-bit length limit. Remembers the first error. */
static void spi_tx(const uint8_t *data, uint32_t len)
{
  while (len > 0U)
  {
    uint16_t n = (len > SPI_MAX_CHUNK) ? SPI_MAX_CHUNK : (uint16_t)len;
    HAL_StatusTypeDef st;

#if ILI9341_USE_DMA
    if (n >= ILI9341_DMA_MIN_BYTES)
    {
      st = HAL_SPI_Transmit_DMA(s_hspi, data, n);
      if (st == HAL_OK)
      {
        uint32_t start = HAL_GetTick();

        /* The DMA transfer-complete interrupt returns the handle to READY. */
        while (HAL_SPI_GetState(s_hspi) != HAL_SPI_STATE_READY)
        {
          if ((HAL_GetTick() - start) > SPI_TIMEOUT_MS)
          {
            (void)HAL_SPI_Abort(s_hspi);
            st = HAL_TIMEOUT;
            break;
          }
        }
      }
    }
    else
#endif
    {
      st = HAL_SPI_Transmit(s_hspi, data, n, SPI_TIMEOUT_MS);
    }

    if ((st != HAL_OK) && (s_status == HAL_OK))
    {
      s_status = st;
    }
    data += n;
    len -= n;
  }
}

static void write_cmd(uint8_t cmd, const uint8_t *params, uint32_t len)
{
  bus_begin();
  dc_command();
  spi_tx(&cmd, 1U);
  dc_data();
  if (len > 0U)
  {
    spi_tx(params, len);
  }
  bus_end();
}

/* Send `count` pixels of one colour into the current window. */
static void write_color(uint16_t color, uint32_t count)
{
  uint32_t fill = (count < (ILI9341_BUF_SIZE / 2U)) ? count : (ILI9341_BUF_SIZE / 2U);

  for (uint32_t i = 0U; i < fill; i++)
  {
    s_buf[2U * i] = (uint8_t)(color >> 8);
    s_buf[(2U * i) + 1U] = (uint8_t)color;
  }

  bus_begin();
  dc_data();
  while (count > 0U)
  {
    uint32_t n = (count < fill) ? count : fill;

    spi_tx(s_buf, 2U * n);
    count -= n;
  }
  bus_end();
}

/* ---- Public API -----------------------------------------------------------*/

HAL_StatusTypeDef ili9341_init(SPI_HandleTypeDef *hspi)
{
  s_hspi = hspi;
  s_status = HAL_OK;

  cs_high();
  dc_data();

  /* Hardware reset */
  HAL_GPIO_WritePin(ILI9341_RST_GPIO_Port, ILI9341_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(1U);
  HAL_GPIO_WritePin(ILI9341_RST_GPIO_Port, ILI9341_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(RESET_PULSE_MS);
  HAL_GPIO_WritePin(ILI9341_RST_GPIO_Port, ILI9341_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(RESET_RECOVER_MS);

  for (uint32_t i = 0U; i < (sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
  {
    write_cmd(init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].len);
  }
  s_width = ILI9341_TFTWIDTH;
  s_height = ILI9341_TFTHEIGHT;

  write_cmd(CMD_SLPOUT, NULL, 0U);
  HAL_Delay(SLPOUT_MS);
  write_cmd(CMD_DISPON, NULL, 0U);

  return s_status;
}

HAL_StatusTypeDef ili9341_status(void)
{
  return s_status;
}

void ili9341_set_rotation(uint8_t r)
{
  uint8_t madctl;

  switch (r & 3U)
  {
    default:
    case 0U: /* portrait, connector at the bottom */
      madctl = MADCTL_MX;
      s_width = ILI9341_TFTWIDTH;
      s_height = ILI9341_TFTHEIGHT;
      break;
    case 1U: /* landscape */
      madctl = MADCTL_MV;
      s_width = ILI9341_TFTHEIGHT;
      s_height = ILI9341_TFTWIDTH;
      break;
    case 2U: /* portrait, rotated 180 degrees */
      madctl = MADCTL_MY;
      s_width = ILI9341_TFTWIDTH;
      s_height = ILI9341_TFTHEIGHT;
      break;
    case 3U: /* landscape, rotated 180 degrees */
      madctl = MADCTL_MX | MADCTL_MY | MADCTL_MV;
      s_width = ILI9341_TFTHEIGHT;
      s_height = ILI9341_TFTWIDTH;
      break;
  }
  madctl |= MADCTL_ORDER;
  write_cmd(CMD_MADCTL, &madctl, 1U);
}

uint16_t ili9341_width(void)
{
  return s_width;
}

uint16_t ili9341_height(void)
{
  return s_height;
}

void ili9341_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t d[4];

  d[0] = (uint8_t)(x0 >> 8);
  d[1] = (uint8_t)x0;
  d[2] = (uint8_t)(x1 >> 8);
  d[3] = (uint8_t)x1;
  write_cmd(CMD_CASET, d, 4U);

  d[0] = (uint8_t)(y0 >> 8);
  d[1] = (uint8_t)y0;
  d[2] = (uint8_t)(y1 >> 8);
  d[3] = (uint8_t)y1;
  write_cmd(CMD_PASET, d, 4U);

  /* Pixel data may follow in later CS frames; the write continues until the
     next command. */
  write_cmd(CMD_RAMWR, NULL, 0U);
}

void ili9341_write_pixels(const uint8_t *data, uint32_t len)
{
  bus_begin();
  dc_data();
  spi_tx(data, len);
  bus_end();
}

void ili9341_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  int32_t x0 = x;
  int32_t y0 = y;
  int32_t x1 = (int32_t)x + w - 1;
  int32_t y1 = (int32_t)y + h - 1;

  if (x0 < 0)
  {
    x0 = 0;
  }
  if (y0 < 0)
  {
    y0 = 0;
  }
  if (x1 >= (int32_t)s_width)
  {
    x1 = (int32_t)s_width - 1;
  }
  if (y1 >= (int32_t)s_height)
  {
    y1 = (int32_t)s_height - 1;
  }
  if ((x1 < x0) || (y1 < y0))
  {
    return;
  }

  ili9341_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
  write_color(color, (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1));
}

void ili9341_fill_screen(uint16_t color)
{
  ili9341_fill_rect(0, 0, (int16_t)s_width, (int16_t)s_height, color);
}

void ili9341_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  if ((w <= 0) || (h <= 0))
  {
    return;
  }
  ili9341_fill_rect(x, y, w, 1, color);
  ili9341_fill_rect(x, (int16_t)(y + h - 1), w, 1, color);
  ili9341_fill_rect(x, y, 1, h, color);
  ili9341_fill_rect((int16_t)(x + w - 1), y, 1, h, color);
}

void ili9341_draw_pixel(int16_t x, int16_t y, uint16_t color)
{
  ili9341_fill_rect(x, y, 1, 1, color);
}

void ili9341_draw_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
  const uint8_t *glyph;
  int32_t s = (scale == 0U) ? 1 : (int32_t)scale;
  int32_t x0 = x;
  int32_t y0 = y;
  int32_t x1 = (int32_t)x + ((int32_t)FONT_CELL_WIDTH * s) - 1;
  int32_t y1 = (int32_t)y + ((int32_t)FONT_CELL_HEIGHT * s) - 1;
  uint32_t n = 0U;

  if ((c < FONT_FIRST_CHAR) || (c > FONT_LAST_CHAR))
  {
    c = '?';
  }
  glyph = font5x7[c - FONT_FIRST_CHAR];

  /* Clip the cell to the screen; only visible pixels are sent. */
  if (x0 < 0)
  {
    x0 = 0;
  }
  if (y0 < 0)
  {
    y0 = 0;
  }
  if (x1 >= (int32_t)s_width)
  {
    x1 = (int32_t)s_width - 1;
  }
  if (y1 >= (int32_t)s_height)
  {
    y1 = (int32_t)s_height - 1;
  }
  if ((x1 < x0) || (y1 < y0))
  {
    return;
  }

  ili9341_set_window((uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);

  bus_begin();
  dc_data();
  for (int32_t py = y0; py <= y1; py++)
  {
    uint32_t row = (uint32_t)((py - y) / s);

    for (int32_t px = x0; px <= x1; px++)
    {
      uint32_t col = (uint32_t)((px - x) / s);
      uint16_t color = bg;

      /* Column FONT_WIDTH and row FONT_HEIGHT are the spacing, always bg. */
      if ((col < FONT_WIDTH) && (row < FONT_HEIGHT) && (((glyph[col] >> row) & 1U) != 0U))
      {
        color = fg;
      }
      s_buf[n++] = (uint8_t)(color >> 8);
      s_buf[n++] = (uint8_t)color;
      if (n == ILI9341_BUF_SIZE)
      {
        spi_tx(s_buf, n);
        n = 0U;
      }
    }
  }
  if (n > 0U)
  {
    spi_tx(s_buf, n);
  }
  bus_end();
}

int16_t ili9341_draw_string(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
  int16_t cx = x;
  int16_t step = (int16_t)(FONT_CELL_WIDTH * ((scale == 0U) ? 1U : scale));
  int16_t line = (int16_t)(FONT_CELL_HEIGHT * ((scale == 0U) ? 1U : scale));

  for (; *s != '\0'; s++)
  {
    if (*s == '\n')
    {
      cx = x;
      y = (int16_t)(y + line);
      continue;
    }
    ili9341_draw_char(cx, y, *s, fg, bg, scale);
    cx = (int16_t)(cx + step);
  }
  return cx;
}
