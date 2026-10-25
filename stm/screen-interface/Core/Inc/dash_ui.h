/**
  ******************************************************************************
  * @file    dash_ui.h
  * @brief   Driver dashboard over two displays.
  *
  *          Main display: ready / fault indicators, speed gauge, state of
  *          charge and the IMU readout.
  *          Second display: motor and battery temperatures, and whether each
  *          CAN bus is receiving.
  ******************************************************************************
  */
#ifndef DASH_UI_H
#define DASH_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pass to a setter when a value is unknown, e.g. no CAN frame for it yet.
   The readout shows "--". */
#define DASH_NO_DATA  INT32_MIN

/* Build the dashboard on the active screens of the two displays. All
   readouts start as "--" and both CAN indicators as off. */
void dash_ui_create(lv_display_t *main_disp, lv_display_t *aux_disp);

/* The setters must run in the same context as lv_timer_handler() (the main
   loop), never in an interrupt: LVGL is not thread-safe. They only redraw
   when the value changes, so calling them on every loop pass is cheap. */

/* Main display */
void dash_ui_set_speed(int32_t speed);
void dash_ui_set_soc(int32_t pct);
void dash_ui_set_status(bool ready, bool fault);

/* IMU readout (main display), one row per IMU output. */
typedef enum
{
  DASH_IMU_ACCEL = 0,     /* x, y, z in m/s^2 */
  DASH_IMU_GYRO,          /* x, y, z in deg/s */
  DASH_IMU_MAG,           /* x, y, z in uT */
  DASH_IMU_EULER,         /* heading, roll, pitch in degrees */
  DASH_IMU_COUNT
} dash_imu_t;

/* Values are in tenths of the row's unit (98 = 9.8 m/s^2). Each shows one
   decimal below 100 and whole numbers from 100 up. DASH_NO_DATA shows "--". */
void dash_ui_set_imu(dash_imu_t row, int32_t a_x10, int32_t b_x10, int32_t c_x10);

/* IMU system calibration level, 0 (none) to 3 (full), or DASH_NO_DATA. */
void dash_ui_set_imu_calib(int32_t level);

/* Second display */
void dash_ui_set_temps(int32_t motor_c, int32_t batt_c);
void dash_ui_set_can_status(bool can1_ok, bool can2_ok);

/* Both displays: number of CAN frames received on either bus since reset,
   shown as "RX <count>". */
void dash_ui_set_can_rx_count(uint32_t count);

/* Show that the configured CAN notification frame was received. */
void dash_ui_show_can_notification(void);

#ifdef __cplusplus
}
#endif

#endif /* DASH_UI_H */
