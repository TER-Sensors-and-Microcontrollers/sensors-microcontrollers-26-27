/**
  ******************************************************************************
  * @file    counter_ui.h
  * @brief   LVGL demo: a ring counter that counts 0..10 once per second,
  *          celebrates at 10, then starts over.
  ******************************************************************************
  */
#ifndef COUNTER_UI_H
#define COUNTER_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Build the counter on the active screen and start it. Everything after this
   runs from lv_timer_handler(). */
void counter_ui_create(void);

#ifdef __cplusplus
}
#endif

#endif /* COUNTER_UI_H */
