/**
  ******************************************************************************
  * @file    dash_ui.c
  * @brief   Driver dashboard: speed gauge, state of charge, motor and battery
  *          temperatures, and ready / fault indicators.
  *
  *          The SPI link only moves ~1.3 MB/s (a full screen is ~120 ms), so
  *          every setter touches only its own widgets and only when the value
  *          changed. Nothing animates and nothing uses opacity on whole
  *          objects or transforms, which would need extra layer buffers.
  *
  *          Text uses the generated dash_font_14/28/72 (tools/gen_fonts.sh),
  *          which hold only the characters drawn here. A character outside
  *          them shows as a placeholder box.
  ******************************************************************************
  */
#include "dash_ui.h"
#include "lvgl.h"

/* ---- Ranges ---------------------------------------------------------------*/
#define SPEED_UNIT          "mph"
#define SPEED_MAX           80      /* gauge full scale */
#define SOC_LOW_PCT         30      /* bar turns amber below this */
#define SOC_CRIT_PCT        15      /* and red below this */
#define TEMP_WARM_C         60      /* readout turns amber from this */
#define TEMP_HOT_C          80      /* and red from this */

/* Thermometer (U+F2C9). LVGL has no LV_SYMBOL_ for it. */
#define SYMBOL_THERMO       "\xEF\x8B\x89"
#define DEGREE              "\xC2\xB0"

/* ---- Look -----------------------------------------------------------------*/
#define COLOR_BG            lv_color_hex(0x10132B)
#define COLOR_TRACK         lv_color_hex(0x252C47)
#define COLOR_TEXT          lv_color_hex(0xF8FAFC)
#define COLOR_MUTED         lv_color_hex(0x8B93B0)
#define COLOR_ACCENT        lv_color_hex(0x22D3EE)
#define COLOR_OK            lv_color_hex(0x34D399)
#define COLOR_WARN          lv_color_hex(0xFBBF24)
#define COLOR_ALERT         lv_color_hex(0xF87171)

/* ---- Layout: 240 x 320 portrait, y is the top edge ------------------------*/
#define MARGIN              14
#define LED_Y               16      /* status icons and captions centre on it */
#define LED_SIZE            12
#define GAUGE_Y             40
#define GAUGE_SIZE          184
#define GAUGE_WIDTH         12
#define SOC_Y               232
#define SOC_BAR_W           78
#define SOC_BAR_H           12
#define TEMP_CAPTION_Y      270
#define TEMP_VALUE_Y        287
#define TEMP_COLUMN_X       60      /* column centres are this far either side */

static lv_obj_t *s_gauge;
static lv_obj_t *s_speed_label;
static lv_obj_t *s_soc_bar;
static lv_obj_t *s_soc_label;
static lv_obj_t *s_motor_temp;
static lv_obj_t *s_batt_temp;
static lv_obj_t *s_ready_led;
static lv_obj_t *s_ready_icon;
static lv_obj_t *s_fault_led;
static lv_obj_t *s_fault_icon;

/* What is on screen now, so a setter can skip an unchanged value. */
static int32_t s_speed;
static int32_t s_soc;
static int32_t s_motor_c;
static int32_t s_batt_c;
static bool s_ready;
static bool s_fault;

/* ---- Helpers --------------------------------------------------------------*/

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

/* Status LED with its icon and caption beside it, on the left or right edge
   of the top row. Returns the LED; the icon is returned through `icon`. */
static lv_obj_t *make_indicator(lv_obj_t *scr, const char *symbol, const char *caption,
                                lv_color_t color, bool right_edge, lv_obj_t **icon)
{
  lv_obj_t *led = lv_led_create(scr);
  lv_obj_t *text;

  lv_obj_set_size(led, LED_SIZE, LED_SIZE);
  lv_led_set_color(led, color);
  lv_led_off(led);

  *icon = make_label(scr, symbol, &dash_font_28, COLOR_MUTED);
  text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  if (right_edge)
  {
    lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -MARGIN, LED_Y);
    lv_obj_align_to(*icon, led, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_align_to(text, *icon, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  }
  else
  {
    lv_obj_align(led, LV_ALIGN_TOP_LEFT, MARGIN, LED_Y);
    lv_obj_align_to(*icon, led, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(text, *icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  }
  return led;
}

static void show_indicator(lv_obj_t *led, lv_obj_t *icon, bool on, lv_color_t color)
{
  if (on)
  {
    lv_led_on(led);
  }
  else
  {
    lv_led_off(led);
  }
  lv_obj_set_style_text_color(icon, on ? color : COLOR_MUTED, 0);
}

static lv_color_t soc_color(int32_t pct)
{
  if (pct < SOC_CRIT_PCT)
  {
    return COLOR_ALERT;
  }
  if (pct < SOC_LOW_PCT)
  {
    return COLOR_WARN;
  }
  return COLOR_OK;
}

static lv_color_t temp_color(int32_t c)
{
  if (c >= TEMP_HOT_C)
  {
    return COLOR_ALERT;
  }
  if (c >= TEMP_WARM_C)
  {
    return COLOR_WARN;
  }
  return COLOR_TEXT;
}

static void show_temp(lv_obj_t *label, int32_t c)
{
  if (c == DASH_NO_DATA)
  {
    lv_label_set_text(label, SYMBOL_THERMO " --" DEGREE "C");
    lv_obj_set_style_text_color(label, COLOR_MUTED, 0);
    return;
  }
  lv_label_set_text_fmt(label, SYMBOL_THERMO " %" LV_PRId32 DEGREE "C", c);
  lv_obj_set_style_text_color(label, temp_color(c), 0);
}

static void make_temp_column(lv_obj_t *scr, const char *caption, int32_t x, lv_obj_t **value)
{
  lv_obj_t *text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  lv_obj_align(text, LV_ALIGN_TOP_MID, x, TEMP_CAPTION_Y);

  *value = make_label(scr, "", &dash_font_28, COLOR_MUTED);
  lv_obj_align(*value, LV_ALIGN_TOP_MID, x, TEMP_VALUE_Y);
  show_temp(*value, DASH_NO_DATA);
}

/* ---- Public API -----------------------------------------------------------*/

void dash_ui_create(void)
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *unit;
  lv_obj_t *battery;

  /* Solid background: a dark gradient bands visibly in RGB565. */
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);

  /* Status row */
  s_ready_led = make_indicator(scr, LV_SYMBOL_CHARGE, "READY", COLOR_OK, false, &s_ready_icon);
  s_fault_led = make_indicator(scr, LV_SYMBOL_WARNING, "FAULT", COLOR_ALERT, true, &s_fault_icon);

  /* Speed: a 270 degree gauge open at the bottom, number in the middle. */
  s_gauge = lv_arc_create(scr);
  lv_obj_set_size(s_gauge, GAUGE_SIZE, GAUGE_SIZE);
  lv_obj_align(s_gauge, LV_ALIGN_TOP_MID, 0, GAUGE_Y);
  lv_arc_set_rotation(s_gauge, 135);
  lv_arc_set_bg_angles(s_gauge, 0, 270);
  lv_arc_set_range(s_gauge, 0, SPEED_MAX);
  lv_arc_set_value(s_gauge, 0);
  lv_obj_remove_style(s_gauge, NULL, LV_PART_KNOB);
  lv_obj_set_clickable(s_gauge, false);
  lv_obj_set_style_arc_width(s_gauge, GAUGE_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_gauge, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_gauge, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_gauge, GAUGE_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_gauge, COLOR_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_gauge, true, LV_PART_INDICATOR);

  s_speed_label = make_label(s_gauge, "--", &dash_font_72, COLOR_MUTED);
  lv_obj_align(s_speed_label, LV_ALIGN_CENTER, 0, -8);

  unit = make_label(s_gauge, SPEED_UNIT, &dash_font_14, COLOR_MUTED);
  lv_obj_align(unit, LV_ALIGN_CENTER, 0, 30);

  /* State of charge: battery icon, bar, percentage. */
  battery = make_label(scr, LV_SYMBOL_BATTERY_FULL, &dash_font_28, COLOR_MUTED);
  lv_obj_align(battery, LV_ALIGN_TOP_LEFT, MARGIN, SOC_Y);

  s_soc_bar = lv_bar_create(scr);
  lv_obj_set_size(s_soc_bar, SOC_BAR_W, SOC_BAR_H);
  lv_obj_align_to(s_soc_bar, battery, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  lv_bar_set_range(s_soc_bar, 0, 100);
  lv_obj_set_style_bg_color(s_soc_bar, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_soc_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_soc_bar, COLOR_OK, LV_PART_INDICATOR);

  s_soc_label = make_label(scr, "--%", &dash_font_28, COLOR_MUTED);
  lv_obj_align(s_soc_label, LV_ALIGN_TOP_RIGHT, -MARGIN, SOC_Y);

  /* Temperatures */
  make_temp_column(scr, "MOTOR", -TEMP_COLUMN_X, &s_motor_temp);
  make_temp_column(scr, "BATT", TEMP_COLUMN_X, &s_batt_temp);

  s_speed = DASH_NO_DATA;
  s_soc = DASH_NO_DATA;
  s_motor_c = DASH_NO_DATA;
  s_batt_c = DASH_NO_DATA;
  s_ready = false;
  s_fault = false;
}

void dash_ui_set_speed(int32_t speed)
{
  if (speed == s_speed)
  {
    return;
  }
  s_speed = speed;

  if (speed == DASH_NO_DATA)
  {
    lv_label_set_text(s_speed_label, "--");
    lv_obj_set_style_text_color(s_speed_label, COLOR_MUTED, 0);
    lv_arc_set_value(s_gauge, 0);
    return;
  }
  lv_label_set_text_fmt(s_speed_label, "%" LV_PRId32, speed);
  lv_obj_set_style_text_color(s_speed_label, COLOR_TEXT, 0);
  lv_arc_set_value(s_gauge, speed);  /* clamped to 0..SPEED_MAX */
}

void dash_ui_set_soc(int32_t pct)
{
  lv_color_t color;

  if (pct == s_soc)
  {
    return;
  }
  s_soc = pct;

  if (pct == DASH_NO_DATA)
  {
    lv_label_set_text(s_soc_label, "--%");
    lv_obj_set_style_text_color(s_soc_label, COLOR_MUTED, 0);
    lv_bar_set_value(s_soc_bar, 0, LV_ANIM_OFF);
    return;
  }
  color = soc_color(pct);
  lv_label_set_text_fmt(s_soc_label, "%" LV_PRId32 "%%", pct);
  lv_obj_set_style_text_color(s_soc_label, (pct < SOC_LOW_PCT) ? color : COLOR_TEXT, 0);
  lv_obj_set_style_bg_color(s_soc_bar, color, LV_PART_INDICATOR);
  lv_bar_set_value(s_soc_bar, pct, LV_ANIM_OFF);
}

void dash_ui_set_temps(int32_t motor_c, int32_t batt_c)
{
  if (motor_c != s_motor_c)
  {
    s_motor_c = motor_c;
    show_temp(s_motor_temp, motor_c);
  }
  if (batt_c != s_batt_c)
  {
    s_batt_c = batt_c;
    show_temp(s_batt_temp, batt_c);
  }
}

void dash_ui_set_status(bool ready, bool fault)
{
  if (ready != s_ready)
  {
    s_ready = ready;
    show_indicator(s_ready_led, s_ready_icon, ready, COLOR_OK);
  }
  if (fault != s_fault)
  {
    s_fault = fault;
    show_indicator(s_fault_led, s_fault_icon, fault, COLOR_ALERT);
  }
}
