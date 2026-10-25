/**
  ******************************************************************************
  * @file    dash_ui.c
  * @brief   Driver dashboard over two displays.
  *
  *          Main display: ready / fault indicators, speed gauge, state of
  *          charge and the IMU readout.
  *          Second display: motor and battery temperatures, and whether each
  *          CAN bus is receiving.
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

/* ---- Ranges ---------------------------------------------------------------*/
#define SPEED_UNIT          "mph"
#define SPEED_MAX           80      /* gauge full scale */
#define SOC_LOW_PCT         30      /* bar turns amber below this */
#define SOC_CRIT_PCT        15      /* and red below this */
#define TEMP_WARM_C         60      /* readout turns amber from this */
#define TEMP_HOT_C          80      /* and red from this */
#define TEMP_BAR_MIN_C      0       /* temperature bar scale */
#define TEMP_BAR_MAX_C      100

/* Thermometer (U+F2C9). LVGL has no LV_SYMBOL_ for it. */
#define SYMBOL_THERMO       "\xEF\x8B\x89"
#define DEGREE              "\xC2\xB0"
#define SQUARED             "\xC2\xB2"
#define MICRO               "\xC2\xB5"

/* ---- Look -----------------------------------------------------------------*/
#define COLOR_BG            lv_color_hex(0x10132B)
#define COLOR_TRACK         lv_color_hex(0x252C47)
#define COLOR_TEXT          lv_color_hex(0xF8FAFC)
#define COLOR_MUTED         lv_color_hex(0x8B93B0)
#define COLOR_ACCENT        lv_color_hex(0x22D3EE)
#define COLOR_OK            lv_color_hex(0x34D399)
#define COLOR_WARN          lv_color_hex(0xFBBF24)
#define COLOR_ALERT         lv_color_hex(0xF87171)

/* ---- Layout: both displays 240 x 320 portrait, y is the top edge ----------*/
#define SCREEN_W            240
#define MARGIN              14
#define LED_SIZE            12

/* Main display, top to bottom: status row, gauge, state of charge, IMU
   table, then a footer with the RX count and the CAN notification. */
#define STATUS_Y            16      /* status icons and captions centre on it */
#define GAUGE_Y             36
#define GAUGE_SIZE          136
#define GAUGE_WIDTH         10
#define GAUGE_SPEED_DY      (-8)    /* offsets from the gauge centre */
#define GAUGE_UNIT_DY       26
#define SOC_Y               174
#define SOC_BAR_W           78
#define SOC_BAR_H           12

/* IMU table: a header row, accel / gyro / mag rows, a second header row,
   then the Euler angles. Values are right-aligned on the column edges. */
#define IMU_Y               208
#define IMU_ROW_H           16
#define IMU_COL_RIGHT_0     102     /* right edge of the first value column */
#define IMU_COL_W           43      /* fits "-2000", the widest value */
#define IMU_UNIT_X          (IMU_COL_RIGHT_0 + (2 * IMU_COL_W) + 6)
#define IMU_HEADING_INSET   16      /* heading centre, left of the column edge */
#define MAIN_FOOTER_Y       304

/* Second display: two temperature panels, then the CAN row. Inside a panel
   the offsets are from the panel's top edge. */
#define MOTOR_PANEL_Y       12
#define BATT_PANEL_Y        142
#define PANEL_VALUE_DY      22      /* top of the 72 px number */
#define PANEL_BAR_DY        90
#define PANEL_BAR_H         10
#define PANEL_VALUE_RIGHT   164     /* the number's right edge, from the left */
#define AUX_NOTIFICATION_Y  252
#define CAN_ROW_Y           288     /* CAN LEDs and captions centre on it */
#define CAN_COLUMN_X        60      /* column centres are this far either side */
#define CAN_RX_Y            300     /* RX count, under the CAN row */

/* One temperature panel on the second display. */
typedef struct
{
  lv_obj_t *icon;
  lv_obj_t *value;
  lv_obj_t *unit;
  lv_obj_t *bar;
  int32_t shown;            /* value on screen, to skip unchanged updates */
} temp_panel_t;

/* Main display */
static lv_obj_t *s_gauge;
static lv_obj_t *s_speed_label;
static lv_obj_t *s_soc_bar;
static lv_obj_t *s_soc_label;
static lv_obj_t *s_ready_led;
static lv_obj_t *s_ready_icon;
static lv_obj_t *s_fault_led;
static lv_obj_t *s_fault_icon;
static lv_obj_t *s_main_can_notification;
static lv_obj_t *s_main_rx_label;
static lv_obj_t *s_imu_value[DASH_IMU_COUNT][3];
static lv_obj_t *s_imu_calib;

/* Second display */
static temp_panel_t s_motor;
static temp_panel_t s_batt;
static lv_obj_t *s_can_led[2];
static lv_obj_t *s_aux_can_notification;
static lv_obj_t *s_aux_rx_label;

/* What is on screen now, so a setter can skip an unchanged value. */
static int32_t s_speed;
static int32_t s_soc;
static bool s_ready;
static bool s_fault;
static bool s_can_ok[2];
static uint32_t s_can_rx_count;
static int32_t s_imu_shown[DASH_IMU_COUNT][3];
static int32_t s_imu_calib_shown;

/* ---- Helpers --------------------------------------------------------------*/

static lv_obj_t *prepare_screen(lv_display_t *disp)
{
  lv_obj_t *scr = lv_display_get_screen_active(disp);

  /* Solid background: a dark gradient bands visibly in RGB565. */
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);
  return scr;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

static lv_obj_t *make_led(lv_obj_t *parent, lv_color_t color)
{
  lv_obj_t *led = lv_led_create(parent);

  lv_obj_set_size(led, LED_SIZE, LED_SIZE);
  lv_led_set_color(led, color);
  lv_led_off(led);
  return led;
}

static lv_obj_t *make_bar(lv_obj_t *parent, int32_t w, int32_t h, int32_t min, int32_t max)
{
  lv_obj_t *bar = lv_bar_create(parent);

  lv_obj_set_size(bar, w, h);
  lv_bar_set_range(bar, min, max);
  lv_obj_set_style_bg_color(bar, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, COLOR_OK, LV_PART_INDICATOR);
  return bar;
}

static void set_led(lv_obj_t *led, bool on)
{
  if (on)
  {
    lv_led_on(led);
  }
  else
  {
    lv_led_off(led);
  }
}

/* Status LED with its icon and caption beside it, on the left or right edge
   of the top row. Returns the LED; the icon is returned through `icon`. */
static lv_obj_t *make_indicator(lv_obj_t *scr, const char *symbol, const char *caption,
                                lv_color_t color, bool right_edge, lv_obj_t **icon)
{
  lv_obj_t *led = make_led(scr, color);
  lv_obj_t *text;

  *icon = make_label(scr, symbol, &dash_font_28, COLOR_MUTED);
  text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  if (right_edge)
  {
    lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -MARGIN, STATUS_Y);
    lv_obj_align_to(*icon, led, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_align_to(text, *icon, LV_ALIGN_OUT_LEFT_MID, -6, 0);
  }
  else
  {
    lv_obj_align(led, LV_ALIGN_TOP_LEFT, MARGIN, STATUS_Y);
    lv_obj_align_to(*icon, led, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_align_to(text, *icon, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
  }
  return led;
}

static void show_indicator(lv_obj_t *led, lv_obj_t *icon, bool on, lv_color_t color)
{
  set_led(led, on);
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

static void show_temp(temp_panel_t *panel, int32_t c)
{
  lv_color_t color;

  if (c == panel->shown)
  {
    return;
  }
  panel->shown = c;

  if (c == DASH_NO_DATA)
  {
    lv_label_set_text(panel->value, "--");
    lv_obj_set_style_text_color(panel->value, COLOR_MUTED, 0);
    lv_obj_set_style_text_color(panel->unit, COLOR_MUTED, 0);
    lv_obj_set_style_text_color(panel->icon, COLOR_MUTED, 0);
    lv_bar_set_value(panel->bar, TEMP_BAR_MIN_C, LV_ANIM_OFF);
    return;
  }
  color = temp_color(c);
  lv_label_set_text_fmt(panel->value, "%" LV_PRId32, c);
  lv_obj_set_style_text_color(panel->value, color, 0);
  lv_obj_set_style_text_color(panel->unit, color, 0);
  lv_obj_set_style_text_color(panel->icon, color, 0);
  /* White text means "normal"; the bar shows normal as green instead. */
  lv_obj_set_style_bg_color(panel->bar, (c >= TEMP_WARM_C) ? color : COLOR_OK, LV_PART_INDICATOR);
  lv_bar_set_value(panel->bar, c, LV_ANIM_OFF);  /* clamped to the bar range */
}

/* Caption, thermometer, big number with its unit, and a bar underneath. */
static void make_temp_panel(lv_obj_t *scr, const char *caption, int32_t y, temp_panel_t *panel)
{
  const int32_t value_y = y + PANEL_VALUE_DY;
  /* Put the unit's baseline on the number's baseline. */
  const int32_t unit_y = value_y + (dash_font_72.line_height - dash_font_72.base_line)
                         - (dash_font_28.line_height - dash_font_28.base_line);
  lv_obj_t *text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  lv_obj_align(text, LV_ALIGN_TOP_LEFT, MARGIN, y);

  panel->icon = make_label(scr, SYMBOL_THERMO, &dash_font_28, COLOR_MUTED);
  lv_obj_align(panel->icon, LV_ALIGN_TOP_LEFT, MARGIN, unit_y);

  /* Right-aligned, so the number grows to the left and the unit stays put. */
  panel->value = make_label(scr, "--", &dash_font_72, COLOR_MUTED);
  lv_obj_align(panel->value, LV_ALIGN_TOP_RIGHT, PANEL_VALUE_RIGHT - SCREEN_W, value_y);

  panel->unit = make_label(scr, DEGREE "C", &dash_font_28, COLOR_MUTED);
  lv_obj_align(panel->unit, LV_ALIGN_TOP_LEFT, PANEL_VALUE_RIGHT + 6, unit_y);

  panel->bar = make_bar(scr, SCREEN_W - (2 * MARGIN), PANEL_BAR_H,
                        TEMP_BAR_MIN_C, TEMP_BAR_MAX_C);
  lv_obj_align(panel->bar, LV_ALIGN_TOP_MID, 0, y + PANEL_BAR_DY);
  lv_bar_set_value(panel->bar, TEMP_BAR_MIN_C, LV_ANIM_OFF);

  panel->shown = DASH_NO_DATA;
}

/* CAN bus LED with its caption to the right, centred on column x. */
static lv_obj_t *make_can_indicator(lv_obj_t *scr, const char *caption, int32_t x)
{
  lv_obj_t *led = make_led(scr, COLOR_OK);
  lv_obj_t *text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  lv_obj_align(text, LV_ALIGN_TOP_MID, x + (LED_SIZE / 2) + 4, CAN_ROW_Y - (dash_font_14.line_height / 2));
  lv_obj_align_to(led, text, LV_ALIGN_OUT_LEFT_MID, -8, 0);
  return led;
}

static lv_obj_t *make_can_notification(lv_obj_t *scr, lv_align_t align, int32_t x, int32_t y)
{
  lv_obj_t *notification = make_label(scr, "CAN 0x123 EVENT", &dash_font_14, COLOR_WARN);

  lv_obj_align(notification, align, x, y);
  lv_obj_add_flag(notification, LV_OBJ_FLAG_HIDDEN);
  return notification;
}

/* Label whose right edge stays at x (from the left) as its text changes. */
static lv_obj_t *make_right_label(lv_obj_t *scr, const char *text, int32_t x, int32_t y, lv_color_t color)
{
  lv_obj_t *label = make_label(scr, text, &dash_font_14, color);

  lv_obj_align(label, LV_ALIGN_TOP_RIGHT, x - SCREEN_W, y);
  return label;
}

static int32_t imu_col_right(uint32_t col)
{
  return IMU_COL_RIGHT_0 + ((int32_t)col * IMU_COL_W);
}

/* One table row: caption, three values and a unit. */
static void make_imu_row(lv_obj_t *scr, dash_imu_t row, const char *caption, const char *unit, int32_t y)
{
  lv_obj_t *text = make_label(scr, caption, &dash_font_14, COLOR_MUTED);

  lv_obj_align(text, LV_ALIGN_TOP_LEFT, MARGIN, y);
  for (uint32_t col = 0U; col < 3U; col++)
  {
    s_imu_value[row][col] = make_right_label(scr, "--", imu_col_right(col), y, COLOR_MUTED);
    s_imu_shown[row][col] = DASH_NO_DATA;
  }
  text = make_label(scr, unit, &dash_font_14, COLOR_MUTED);
  lv_obj_align(text, LV_ALIGN_TOP_LEFT, IMU_UNIT_X, y);
}

/* Column headings, centred over a typical value ("-39.2") so that "ROLL"
   and "PITCH" do not run together. */
static void make_imu_heading(lv_obj_t *scr, const char *const names[3], int32_t y)
{
  for (uint32_t col = 0U; col < 3U; col++)
  {
    lv_obj_t *label = make_label(scr, names[col], &dash_font_14, COLOR_MUTED);

    lv_obj_align(label, LV_ALIGN_TOP_MID, imu_col_right(col) - IMU_HEADING_INSET - (SCREEN_W / 2), y);
  }
}

static void make_imu_table(lv_obj_t *scr)
{
  static const char *const xyz[3] = {"X", "Y", "Z"};
  static const char *const hrp[3] = {"HDG", "ROLL", "PITCH"};
  lv_obj_t *title = make_label(scr, "IMU", &dash_font_14, COLOR_ACCENT);

  lv_obj_align(title, LV_ALIGN_TOP_LEFT, MARGIN, IMU_Y);
  make_imu_heading(scr, xyz, IMU_Y);
  s_imu_calib = make_label(scr, "CAL -", &dash_font_14, COLOR_MUTED);
  lv_obj_align(s_imu_calib, LV_ALIGN_TOP_LEFT, IMU_UNIT_X, IMU_Y);
  s_imu_calib_shown = DASH_NO_DATA;

  make_imu_row(scr, DASH_IMU_ACCEL, "ACC", "m/s" SQUARED, IMU_Y + (1 * IMU_ROW_H));
  make_imu_row(scr, DASH_IMU_GYRO, "GYRO", DEGREE "/s", IMU_Y + (2 * IMU_ROW_H));
  make_imu_row(scr, DASH_IMU_MAG, "MAG", MICRO "T", IMU_Y + (3 * IMU_ROW_H));
  make_imu_heading(scr, hrp, IMU_Y + (4 * IMU_ROW_H));
  make_imu_row(scr, DASH_IMU_EULER, "ANGLE", DEGREE, IMU_Y + (5 * IMU_ROW_H));
}

/* Tenths as text: "-9.8" below 100, "-123" (rounded) from 100 up. */
static void format_tenths(char *buf, uint32_t size, int32_t x10)
{
  const char *sign = (x10 < 0) ? "-" : "";
  const int32_t mag = (x10 < 0) ? -x10 : x10;

  if (mag < 1000)
  {
    lv_snprintf(buf, size, "%s%" LV_PRId32 ".%" LV_PRId32, sign, mag / 10, mag % 10);
  }
  else
  {
    lv_snprintf(buf, size, "%s%" LV_PRId32, sign, (mag + 5) / 10);
  }
}

/* ---- Screens --------------------------------------------------------------*/

static void create_main_screen(lv_display_t *disp)
{
  lv_obj_t *scr = prepare_screen(disp);
  lv_obj_t *unit;
  lv_obj_t *battery;

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
  lv_obj_align(s_speed_label, LV_ALIGN_CENTER, 0, GAUGE_SPEED_DY);

  unit = make_label(s_gauge, SPEED_UNIT, &dash_font_14, COLOR_MUTED);
  lv_obj_align(unit, LV_ALIGN_CENTER, 0, GAUGE_UNIT_DY);

  /* State of charge: battery icon, bar, percentage. */
  battery = make_label(scr, LV_SYMBOL_BATTERY_FULL, &dash_font_28, COLOR_MUTED);
  lv_obj_align(battery, LV_ALIGN_TOP_LEFT, MARGIN, SOC_Y);

  s_soc_bar = make_bar(scr, SOC_BAR_W, SOC_BAR_H, 0, 100);
  lv_obj_align_to(s_soc_bar, battery, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  s_soc_label = make_label(scr, "--%", &dash_font_28, COLOR_MUTED);
  lv_obj_align(s_soc_label, LV_ALIGN_TOP_RIGHT, -MARGIN, SOC_Y);

  make_imu_table(scr);

  /* Footer: RX count on the left, notification on the right. */
  s_main_rx_label = make_label(scr, "RX 0", &dash_font_14, COLOR_MUTED);
  lv_obj_align(s_main_rx_label, LV_ALIGN_TOP_LEFT, MARGIN, MAIN_FOOTER_Y);
  s_main_can_notification = make_can_notification(scr, LV_ALIGN_TOP_RIGHT, -MARGIN, MAIN_FOOTER_Y);
  s_speed = DASH_NO_DATA;
  s_soc = DASH_NO_DATA;
  s_ready = false;
  s_fault = false;
}

static void create_aux_screen(lv_display_t *disp)
{
  lv_obj_t *scr = prepare_screen(disp);

  make_temp_panel(scr, "MOTOR", MOTOR_PANEL_Y, &s_motor);
  make_temp_panel(scr, "BATTERY", BATT_PANEL_Y, &s_batt);

  s_can_led[0] = make_can_indicator(scr, "CAN1", -CAN_COLUMN_X);
  s_can_led[1] = make_can_indicator(scr, "CAN2", CAN_COLUMN_X);
  s_aux_can_notification = make_can_notification(scr, LV_ALIGN_TOP_MID, 0, AUX_NOTIFICATION_Y);

  s_aux_rx_label = make_label(scr, "RX 0", &dash_font_14, COLOR_MUTED);
  lv_obj_align(s_aux_rx_label, LV_ALIGN_TOP_MID, 0, CAN_RX_Y);

  s_can_ok[0] = false;
  s_can_ok[1] = false;
  s_can_rx_count = 0U;
}

/* ---- Public API -----------------------------------------------------------*/

void dash_ui_create(lv_display_t *main_disp, lv_display_t *aux_disp)
{
  create_main_screen(main_disp);
  create_aux_screen(aux_disp);
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

void dash_ui_set_temps(int32_t motor_c, int32_t batt_c)
{
  show_temp(&s_motor, motor_c);
  show_temp(&s_batt, batt_c);
}

void dash_ui_set_can_status(bool can1_ok, bool can2_ok)
{
  const bool ok[2] = {can1_ok, can2_ok};

  for (uint32_t i = 0U; i < 2U; i++)
  {
    if (ok[i] != s_can_ok[i])
    {
      s_can_ok[i] = ok[i];
      set_led(s_can_led[i], ok[i]);
    }
  }
}

void dash_ui_set_imu(dash_imu_t row, int32_t a_x10, int32_t b_x10, int32_t c_x10)
{
  const int32_t v[3] = {a_x10, b_x10, c_x10};
  char buf[12];

  if (row >= DASH_IMU_COUNT)
  {
    return;
  }
  for (uint32_t col = 0U; col < 3U; col++)
  {
    lv_obj_t *label = s_imu_value[row][col];

    if (v[col] == s_imu_shown[row][col])
    {
      continue;
    }
    if (v[col] == DASH_NO_DATA)
    {
      lv_label_set_text(label, "--");
      lv_obj_set_style_text_color(label, COLOR_MUTED, 0);
    }
    else
    {
      format_tenths(buf, sizeof(buf), v[col]);
      lv_label_set_text(label, buf);
      if (s_imu_shown[row][col] == DASH_NO_DATA)
      {
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
      }
    }
    s_imu_shown[row][col] = v[col];
  }
}

void dash_ui_set_imu_calib(int32_t level)
{
  lv_color_t color;

  if (level == s_imu_calib_shown)
  {
    return;
  }
  s_imu_calib_shown = level;

  if (level == DASH_NO_DATA)
  {
    lv_label_set_text(s_imu_calib, "CAL -");
    lv_obj_set_style_text_color(s_imu_calib, COLOR_MUTED, 0);
    return;
  }
  if (level >= 3)
  {
    color = COLOR_OK;
  }
  else if (level >= 1)
  {
    color = COLOR_WARN;
  }
  else
  {
    color = COLOR_ALERT;
  }
  lv_label_set_text_fmt(s_imu_calib, "CAL %" LV_PRId32, level);
  lv_obj_set_style_text_color(s_imu_calib, color, 0);
}

void dash_ui_set_can_rx_count(uint32_t count)
{
  if (count == s_can_rx_count)
  {
    return;
  }
  s_can_rx_count = count;

  /* Centre-aligned labels re-centre themselves as the text grows. */
  lv_label_set_text_fmt(s_main_rx_label, "RX %" LV_PRIu32, count);
  lv_label_set_text_fmt(s_aux_rx_label, "RX %" LV_PRIu32, count);
}

void dash_ui_show_can_notification(void)
{
  lv_obj_clear_flag(s_main_can_notification, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(s_aux_can_notification, LV_OBJ_FLAG_HIDDEN);
}
