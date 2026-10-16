/**
  ******************************************************************************
  * @file    dash_mock.h
  * @brief   Made-up sensor values for the dashboard, for testing it without
  *          the car.
  ******************************************************************************
  */
#ifndef DASH_MOCK_H
#define DASH_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start feeding values into the dash_ui_set_*() setters from an LVGL timer.
   Call after dash_ui_create(). */
void dash_mock_start(void);

#ifdef __cplusplus
}
#endif

#endif /* DASH_MOCK_H */
