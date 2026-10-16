/**
  ******************************************************************************
  * @file    dash_mock.c
  * @brief   Made-up sensor values for the dashboard, for testing it without
  *          the car. The real feed replaces this: decode CAN frames into
  *          variables, then call the same dash_ui_set_*() setters from the
  *          main loop.
  *
  *          Every value sweeps through its colour thresholds, and the speed
  *          drops out periodically to show the "--" no-data state.
  ******************************************************************************
  */
#include "dash_mock.h"
#include "dash_ui.h"
#include "lvgl.h"

#define MOCK_PERIOD_MS      100U

/* Speed: up and down 0..SPEED_PEAK, one mph per tick. */
#define SPEED_PEAK          72U
#define NO_DATA_EVERY       200U    /* ticks between speed dropouts */
#define NO_DATA_TICKS       20U     /* how long a dropout lasts */

/* State of charge falls one percent every SOC_TICKS, then refills. */
#define SOC_TICKS           10U

/* Motor temperature sweeps TEMP_MIN..TEMP_MAX, one degree every TEMP_TICKS. */
#define TEMP_MIN            35U
#define TEMP_MAX            95U
#define TEMP_TICKS          5U
#define FAULT_TEMP_C        90      /* fault indicator on from here */

static uint32_t s_tick;

/* 0..peak..0 as `t` counts up. */
static uint32_t triangle(uint32_t t, uint32_t peak)
{
  uint32_t phase = t % (2U * peak);

  return (phase < peak) ? phase : (2U * peak) - phase;
}

static void mock_tick(lv_timer_t *t)
{
  int32_t speed;
  int32_t soc;
  int32_t motor_c;
  int32_t batt_c;

  LV_UNUSED(t);

  speed = (int32_t)triangle(s_tick, SPEED_PEAK);
  if ((s_tick % NO_DATA_EVERY) >= (NO_DATA_EVERY - NO_DATA_TICKS))
  {
    speed = DASH_NO_DATA;
  }

  soc = 100 - (int32_t)((s_tick / SOC_TICKS) % 101U);
  motor_c = (int32_t)(TEMP_MIN + triangle(s_tick / TEMP_TICKS, TEMP_MAX - TEMP_MIN));
  batt_c = 25 + ((motor_c - (int32_t)TEMP_MIN) / 2);

  dash_ui_set_speed(speed);
  dash_ui_set_soc(soc);
  dash_ui_set_temps(motor_c, batt_c);
  dash_ui_set_status(motor_c < FAULT_TEMP_C, motor_c >= FAULT_TEMP_C);

  s_tick++;
}

void dash_mock_start(void)
{
  lv_timer_t *t = lv_timer_create(mock_tick, MOCK_PERIOD_MS, NULL);

  lv_timer_ready(t);  /* first values on the next lv_timer_handler() */
}
