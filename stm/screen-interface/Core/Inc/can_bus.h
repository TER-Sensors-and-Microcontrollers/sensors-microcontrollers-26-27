/**
  ******************************************************************************
  * @file    can_bus.h
  * @brief   Receive and send classic CAN frames on the two FDCAN peripherals.
  *
  *          CAN_BUS_1 is FDCAN1 (PA11 RX, PA12 TX) and CAN_BUS_2 is FDCAN3
  *          (PA8 RX, PA15 TX). Both run at 500 kbit/s; the bit timing is set
  *          in CubeMX (screen-interface.ioc).
  *
  *          The FDCAN RX FIFO holds only 3 frames, so the receive interrupt
  *          copies each frame into a software queue per bus. The main loop
  *          takes them out with can_bus_receive(), so frame handling (and any
  *          LVGL call it makes) stays out of the interrupt.
  ******************************************************************************
  */
#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  CAN_BUS_1 = 0,
  CAN_BUS_2,
  CAN_BUS_COUNT
} can_bus_t;

typedef struct
{
  uint32_t id;            /* 11-bit or 29-bit identifier */
  bool     extended;      /* true for a 29-bit identifier */
  uint8_t  len;           /* 0..8 data bytes */
  uint8_t  data[8];
  uint32_t rx_tick;       /* HAL_GetTick() when it arrived */
} can_frame_t;

/* Accept every data frame on both buses, enable the receive interrupt and
   start both controllers. Call once, after MX_FDCANx_Init(). Returns false
   if a controller did not start. */
bool can_bus_init(FDCAN_HandleTypeDef *bus1, FDCAN_HandleTypeDef *bus2);

/* Take the oldest received frame off `bus`'s queue. Returns false when the
   queue is empty. Call from the main loop until it returns false. */
bool can_bus_receive(can_bus_t bus, can_frame_t *frame);

/* Queue a data frame for sending. Returns false if the TX FIFO is full, the
   bus is off, or len > 8. */
bool can_bus_send(can_bus_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t len);

/* True if `bus` received a frame in the last `timeout_ms` and is not
   bus-off. Use it to show whether a bus is alive. */
bool can_bus_is_active(can_bus_t bus, uint32_t timeout_ms);

/* Frames lost because the software queue was full (the main loop did not
   keep up). */
uint32_t can_bus_dropped(can_bus_t bus);

/* Call from the main loop. Restarts a controller that has gone bus-off
   (too many errors, e.g. no transceiver, no termination or the wrong
   bitrate), so it rejoins the bus once the fault is fixed. */
void can_bus_service(void);

#ifdef __cplusplus
}
#endif

#endif /* CAN_BUS_H */
