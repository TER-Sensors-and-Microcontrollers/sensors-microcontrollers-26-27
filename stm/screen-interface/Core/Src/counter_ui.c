/**
  ******************************************************************************
  * @file    counter_ui.c
  * @brief   LVGL demo: a ring counter that counts 0..10 once per second,
  *          celebrates at 10, then starts over.
  *
  *          The SPI link only moves ~1.3 MB/s, so the animations are picked to
  *          redraw small areas: the number slides a few pixels, the ring only
  *          redraws the arc segment that changed, and confetti pieces are tiny.
  *          Nothing uses opacity on whole objects or transforms, which would
  *          need extra layer buffers.
  ******************************************************************************
  */
#include "counter_ui.h"
#include "lvgl.h"

/* ---- Behaviour ------------------------------------------------------------*/
#define COUNT_MAX           10
#define COUNT_PERIOD_MS     1000U
#define CELEBRATE_MS        3000U   /* how long "Nice work!" stays up */

/* The ring runs 0..COUNT_MAX * ARC_STEPS so its fill can be animated
   smoothly between whole counts. */
#define ARC_STEPS           100

/* ---- Confetti physics (pixels, seconds) -----------------------------------*/
#define CONFETTI_COUNT      20
#define CONFETTI_MS         2200U   /* every piece has fallen off-screen by then */
#define CONFETTI_GRAVITY    420     /* px/s^2 */
#define CONFETTI_VX_MAX     110     /* horizontal speed, +/- px/s */
#define CONFETTI_VY_MIN     140     /* launch speed upwards, px/s */
#define CONFETTI_VY_MAX     320

/* ---- Look -----------------------------------------------------------------*/
#define COLOR_BG            lv_color_hex(0x10132B)
#define COLOR_TRACK         lv_color_hex(0x252C47)
#define COLOR_TEXT          lv_color_hex(0xF8FAFC)
#define COLOR_MUTED         lv_color_hex(0x8B93B0)
#define COLOR_ACCENT_START  lv_color_hex(0x22D3EE)   /* ring colour at 0 */
#define COLOR_ACCENT_END    lv_color_hex(0xA855F7)   /* ring colour at COUNT_MAX - 1 */
#define COLOR_GOLD          lv_color_hex(0xFBBF24)
#define COLOR_CARD          lv_color_hex(0x17132E)

#define ARC_SIZE            192
#define ARC_WIDTH           14

typedef struct
{
  int16_t x0, y0;  /* launch point */
  int16_t vx, vy;  /* launch velocity, px/s */
} confetti_t;

static const uint32_t confetti_colors[] = {
  0xFBBF24, 0xF472B6, 0x22D3EE, 0xA855F7, 0x34D399, 0xF8FAFC,
};

static lv_obj_t *s_arc;
static lv_obj_t *s_number;
static lv_obj_t *s_of_label;
static lv_obj_t *s_caption;
static lv_obj_t *s_card;
static lv_obj_t *s_confetti[CONFETTI_COUNT];
static confetti_t s_confetti_state[CONFETTI_COUNT];
static lv_timer_t *s_count_timer;
static int32_t s_count;

/* ---- Animation helpers ----------------------------------------------------*/

static void set_arc_value(void *obj, int32_t v)
{
  lv_arc_set_value((lv_obj_t *)obj, v);
}

static void set_translate_y(void *obj, int32_t v)
{
  lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void set_text_opa(void *obj, int32_t v)
{
  lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void anim_run(lv_obj_t *obj, lv_anim_exec_xcb_t exec_cb, int32_t from, int32_t to,
                     uint32_t duration, lv_anim_path_cb_t path_cb)
{
  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, exec_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, duration);
  lv_anim_set_path_cb(&a, path_cb);
  lv_anim_start(&a);
}

/* ---- Counter --------------------------------------------------------------*/

/* Ring colour drifts from cyan to purple as the count goes up. */
static lv_color_t accent_for(int32_t count)
{
  lv_opa_t mix = (lv_opa_t)((count * 255) / (COUNT_MAX - 1));

  return lv_color_mix(COLOR_ACCENT_END, COLOR_ACCENT_START, mix);
}

/* Show `count`: the number drops in from above and the ring sweeps to it. */
static void show_count(int32_t count, uint32_t sweep_ms)
{
  lv_label_set_text_fmt(s_number, "%" LV_PRId32, count);
  anim_run(s_number, set_translate_y, -14, 0, 280, lv_anim_path_ease_out);
  anim_run(s_number, set_text_opa, LV_OPA_TRANSP, LV_OPA_COVER, 280, lv_anim_path_ease_out);

  anim_run(s_arc, set_arc_value, lv_arc_get_value(s_arc), count * ARC_STEPS, sweep_ms,
           lv_anim_path_ease_in_out);
}

/* ---- Celebration ----------------------------------------------------------*/

/* Ballistic flight: the animation value is the time since launch in ms. */
static void confetti_step(void *var, int32_t t)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  const confetti_t *c = (const confetti_t *)lv_obj_get_user_data(obj);
  int32_t x = c->x0 + ((c->vx * t) / 1000);
  int32_t y = c->y0 + ((c->vy * t) / 1000) + (((CONFETTI_GRAVITY * t) / 1000) * t) / 2000;

  lv_obj_set_pos(obj, x, y);
}

static void confetti_launch(void)
{
  lv_obj_t *scr = lv_screen_active();
  lv_area_t arc_area;
  int16_t cx;
  int16_t cy;

  lv_obj_get_coords(s_arc, &arc_area);
  cx = (int16_t)((arc_area.x1 + arc_area.x2) / 2);
  cy = (int16_t)((arc_area.y1 + arc_area.y2) / 2);

  for (uint32_t i = 0; i < CONFETTI_COUNT; i++)
  {
    lv_obj_t *p = lv_obj_create(scr);
    confetti_t *c = &s_confetti_state[i];
    int32_t w = (int32_t)lv_rand(4, 9);
    int32_t h = (int32_t)lv_rand(4, 9);
    uint32_t color = confetti_colors[i % (sizeof(confetti_colors) / sizeof(confetti_colors[0]))];

    lv_obj_remove_style_all(p);
    lv_obj_set_clickable(p, false);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_radius(p, 2, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);

    c->x0 = (int16_t)(cx - (w / 2));
    c->y0 = (int16_t)(cy - (h / 2));
    c->vx = (int16_t)((int32_t)lv_rand(0, 2 * CONFETTI_VX_MAX) - CONFETTI_VX_MAX);
    c->vy = (int16_t)(-(int32_t)lv_rand(CONFETTI_VY_MIN, CONFETTI_VY_MAX));
    lv_obj_set_user_data(p, c);
    lv_obj_set_pos(p, c->x0, c->y0);

    anim_run(p, confetti_step, 0, (int32_t)CONFETTI_MS, CONFETTI_MS, lv_anim_path_linear);
    s_confetti[i] = p;
  }
}

static void confetti_clear(void)
{
  for (uint32_t i = 0; i < CONFETTI_COUNT; i++)
  {
    if (s_confetti[i] != NULL)
    {
      lv_obj_delete(s_confetti[i]);  /* also stops its animation */
      s_confetti[i] = NULL;
    }
  }
}

static void card_hide_cb(lv_anim_t *a)
{
  LV_UNUSED(a);
  lv_obj_set_hidden(s_card, true);
}

static void celebrate_end(lv_timer_t *t)
{
  lv_anim_t a;

  LV_UNUSED(t);

  confetti_clear();

  /* Slide the card back down, then hide it. */
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_card);
  lv_anim_set_exec_cb(&a, set_translate_y);
  lv_anim_set_values(&a, 0, 90);
  lv_anim_set_duration(&a, 250);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&a, card_hide_cb);
  lv_anim_start(&a);

  lv_obj_set_hidden(s_caption, false);
  lv_obj_set_style_text_color(s_number, COLOR_TEXT, 0);
  lv_obj_set_style_text_color(s_of_label, COLOR_MUTED, 0);
  lv_obj_set_style_arc_color(s_arc, accent_for(0), LV_PART_INDICATOR);

  /* Rewind the ring to 0 and start counting again. */
  s_count = 0;
  show_count(s_count, 600);
  lv_timer_reset(s_count_timer);
  lv_timer_resume(s_count_timer);
}

static void celebrate_start(void)
{
  lv_timer_t *t;

  lv_timer_pause(s_count_timer);

  lv_obj_set_style_text_color(s_number, COLOR_GOLD, 0);
  lv_obj_set_style_text_color(s_of_label, COLOR_GOLD, 0);
  lv_obj_set_style_arc_color(s_arc, COLOR_GOLD, LV_PART_INDICATOR);

  lv_obj_set_hidden(s_caption, true);
  lv_obj_set_hidden(s_card, false);
  anim_run(s_card, set_translate_y, 90, 0, 450, lv_anim_path_overshoot);

  confetti_launch();

  t = lv_timer_create(celebrate_end, CELEBRATE_MS, NULL);
  lv_timer_set_repeat_count(t, 1);  /* one-shot, deletes itself */
}

static void count_tick(lv_timer_t *t)
{
  LV_UNUSED(t);

  s_count++;
  show_count(s_count, 400);

  if (s_count >= COUNT_MAX)
  {
    celebrate_start();
  }
  else
  {
    lv_obj_set_style_arc_color(s_arc, accent_for(s_count), LV_PART_INDICATOR);
  }
}

/* ---- Layout ---------------------------------------------------------------*/

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

void counter_ui_create(void)
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *title;
  lv_obj_t *underline;
  lv_obj_t *card_title;
  lv_obj_t *card_sub;

  /* Solid background: a dark gradient bands visibly in RGB565. Confetti
     flies past the edges, so the screen must not turn scrollable. */
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);

  /* Header */
  title = make_label(scr, "COUNTER", &lv_font_montserrat_14, COLOR_MUTED);
  lv_obj_set_style_text_letter_space(title, 4, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 2, 18);  /* +2 offsets the trailing letter space */

  underline = lv_obj_create(scr);
  lv_obj_remove_style_all(underline);
  lv_obj_set_size(underline, 28, 3);
  lv_obj_set_style_radius(underline, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(underline, COLOR_ACCENT_START, 0);
  lv_obj_set_style_bg_opa(underline, LV_OPA_COVER, 0);
  lv_obj_align_to(underline, title, LV_ALIGN_OUT_BOTTOM_MID, -2, 6);

  /* Progress ring: a 270 degree gauge open at the bottom. */
  s_arc = lv_arc_create(scr);
  lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
  lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -2);
  lv_arc_set_rotation(s_arc, 135);
  lv_arc_set_bg_angles(s_arc, 0, 270);
  lv_arc_set_range(s_arc, 0, COUNT_MAX * ARC_STEPS);
  lv_arc_set_value(s_arc, 0);
  lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
  lv_obj_set_clickable(s_arc, false);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, COLOR_TRACK, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, accent_for(0), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);

  s_number = make_label(s_arc, "0", &lv_font_montserrat_48, COLOR_TEXT);
  lv_obj_align(s_number, LV_ALIGN_CENTER, 0, -8);

  s_of_label = make_label(s_arc, "", &lv_font_montserrat_14, COLOR_MUTED);
  lv_label_set_text_fmt(s_of_label, "of %d", COUNT_MAX);
  lv_obj_align(s_of_label, LV_ALIGN_CENTER, 0, 30);

  s_caption = make_label(scr, "+1 every second", &lv_font_montserrat_14, COLOR_MUTED);
  lv_obj_align(s_caption, LV_ALIGN_BOTTOM_MID, 0, -26);

  /* Celebration card, hidden until the count reaches COUNT_MAX. */
  s_card = lv_obj_create(scr);
  lv_obj_remove_style_all(s_card);
  lv_obj_set_size(s_card, 200, 58);
  lv_obj_align(s_card, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_radius(s_card, 18, 0);
  lv_obj_set_style_bg_color(s_card, COLOR_CARD, 0);
  lv_obj_set_style_bg_opa(s_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(s_card, COLOR_GOLD, 0);
  lv_obj_set_style_border_width(s_card, 2, 0);
  lv_obj_set_clickable(s_card, false);
  lv_obj_set_scrollable(s_card, false);
  lv_obj_set_hidden(s_card, true);

  card_title = make_label(s_card, LV_SYMBOL_OK "  Nice work!", &lv_font_montserrat_20, COLOR_GOLD);
  lv_obj_align(card_title, LV_ALIGN_TOP_MID, 0, 8);

  card_sub = make_label(s_card, "", &lv_font_montserrat_14, COLOR_MUTED);
  lv_label_set_text_fmt(card_sub, "You counted to %d", COUNT_MAX);
  lv_obj_align(card_sub, LV_ALIGN_BOTTOM_MID, 0, -8);

  s_count = 0;
  s_count_timer = lv_timer_create(count_tick, COUNT_PERIOD_MS, NULL);
}
