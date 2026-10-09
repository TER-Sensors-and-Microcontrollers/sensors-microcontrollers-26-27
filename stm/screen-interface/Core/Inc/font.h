/**
  ******************************************************************************
  * @file    font.h
  * @brief   Built-in 5x7 bitmap font (printable ASCII 0x20..0x7E).
  ******************************************************************************
  */
#ifndef FONT_H
#define FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FONT_WIDTH        5U    /* glyph columns */
#define FONT_HEIGHT       7U    /* glyph rows */
#define FONT_CELL_WIDTH   6U    /* glyph + 1 column of spacing */
#define FONT_CELL_HEIGHT  8U    /* glyph + 1 row of spacing */
#define FONT_FIRST_CHAR   0x20
#define FONT_LAST_CHAR    0x7E

/* One glyph = FONT_WIDTH bytes, one byte per column, left to right.
   Bit 0 is the top row, bit 6 the bottom row. */
extern const uint8_t font5x7[FONT_LAST_CHAR - FONT_FIRST_CHAR + 1][FONT_WIDTH];

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
