#!/usr/bin/env bash
# Regenerates the dashboard fonts in Core/Src/fonts/ with lv_font_conv.
#
# The fonts hold only the characters the dashboard draws, which is what keeps
# them small: LVGL's built-in Montserrat fonts carry all of ASCII plus ~60
# icons, so the 48 px one alone is ~95 KB of flash. If a label shows a box
# instead of a character, add the character's range here and rerun.
#
# Needs Node.js (npx). Run from anywhere: tools/gen_fonts.sh
set -euo pipefail

cd "$(dirname "$0")/.."

CONV="npx --yes lv_font_conv@1.5.3"
SRC=lvgl/scripts/generators/built_in_font
TEXT="$SRC/Montserrat-Medium.ttf"
ICONS="$SRC/FontAwesome5-Solid+Brands+Regular.woff"
OUT=Core/Src/fonts

# Uncompressed 4 bpp glyphs, the same format as LVGL's built-in fonts, so
# LV_USE_FONT_COMPRESSED can stay off.
COMMON=(--bpp 4 --no-compress --format lvgl --lv-include lvgl.h)

# Icons: warning (LV_SYMBOL_WARNING), battery (LV_SYMBOL_BATTERY_FULL),
# bolt (LV_SYMBOL_CHARGE), thermometer (DASH_SYMBOL_THERMO in dash_ui.c).
ICON_RANGE=0xF071,0xF240,0xF0E7,0xF2C9

mkdir -p "$OUT"

# Default font: captions and units. ASCII plus the degree, superscript two
# and micro signs (m/s² and µT in the IMU readout).
$CONV "${COMMON[@]}" --size 14 --lv-font-name dash_font_14 \
  --font "$TEXT" -r 0x20-0x7E,0xB0,0xB2,0xB5 \
  -o "$OUT/dash_font_14.c"

# Secondary readouts: digits, sign, decimal point, % V A kW C and degree,
# plus the status icons.
$CONV "${COMMON[@]}" --size 28 --lv-font-name dash_font_28 \
  --font "$TEXT" -r 0x20,0x25,0x2D-0x2E,0x30-0x39,0x41,0x43,0x56,0x57,0x6B,0xB0 \
  --font "$ICONS" -r "$ICON_RANGE" \
  -o "$OUT/dash_font_28.c"

# Speed: digits, and '-' for the "--" no-data state.
$CONV "${COMMON[@]}" --size 72 --lv-font-name dash_font_72 \
  --font "$TEXT" -r 0x2D,0x30-0x39 \
  -o "$OUT/dash_font_72.c"
