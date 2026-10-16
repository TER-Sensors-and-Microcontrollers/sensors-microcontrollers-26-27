/**
  ******************************************************************************
  * @file    dash_ui.h
  * @brief   Driver dashboard: speed gauge, state of charge, motor and battery
  *          temperatures, and ready / fault indicators.
  ******************************************************************************
  */
#ifndef DASH_UI_H
#define DASH_UI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pass to a setter when a value is unknown, e.g. no CAN frame for it yet.
   The readout shows "--". */
#define DASH_NO_DATA  INT32_MIN

/* Build the dashboard on the active screen. All readouts start as "--". */
void dash_ui_create(void);

/* The setters must run in the same context as lv_timer_handler() (the main
   loop), never in an interrupt: LVGL is not thread-safe. They only redraw
   when the value changes, so calling them on every loop pass is cheap. */
void dash_ui_set_speed(int32_t speed);
void dash_ui_set_soc(int32_t pct);
void dash_ui_set_temps(int32_t motor_c, int32_t batt_c);
void dash_ui_set_status(bool ready, bool fault);

#ifdef __cplusplus
}
#endif

#endif /* DASH_UI_H */
