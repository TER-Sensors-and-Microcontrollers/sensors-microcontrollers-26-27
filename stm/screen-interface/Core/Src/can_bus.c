/**
  ******************************************************************************
  * @file    can_bus.c
  * @brief   Receive and send classic CAN frames on the two FDCAN peripherals.
  ******************************************************************************
  */
#include "can_bus.h"

#include <string.h>

/* Frames each bus can buffer between two main loop passes. Must be a power
   of two. At 500 kbit/s a bus carries at most ~4400 frames/s, so 32 frames
   covers about 7 ms of a fully loaded bus. */
#define RX_QUEUE_LEN    32U

/* A classic frame may carry a DLC of 9..15 (still 8 data bytes), and
   HAL_FDCAN_GetRxMessage() then copies the CAN FD length for that code, up
   to 64 bytes. Receive into a buffer that big, then keep the first 8. */
#define RX_SCRATCH_LEN  64U

typedef struct
{
  FDCAN_HandleTypeDef *hfdcan;
  can_frame_t queue[RX_QUEUE_LEN];
  volatile uint32_t head;          /* written by the interrupt only */
  volatile uint32_t tail;          /* written by the main loop only */
  volatile uint32_t dropped;
  volatile uint32_t last_rx_tick;
  volatile bool     seen;          /* at least one frame received */
} bus_state_t;

static bus_state_t s_bus[CAN_BUS_COUNT];

static bus_state_t *find_bus(const FDCAN_HandleTypeDef *hfdcan)
{
  for (uint32_t i = 0U; i < CAN_BUS_COUNT; i++)
  {
    if (s_bus[i].hfdcan == hfdcan)
    {
      return &s_bus[i];
    }
  }
  return NULL;
}

static bool is_bus_off(const FDCAN_HandleTypeDef *hfdcan)
{
  return (hfdcan->Instance->PSR & FDCAN_PSR_BO) != 0U;
}

static bool start_bus(FDCAN_HandleTypeDef *hfdcan)
{
  /* No filters: every data frame goes to RX FIFO 0. Add HAL_FDCAN_ConfigFilter()
     calls (and raise StdFiltersNbr / ExtFiltersNbr in CubeMX) to keep only the
     IDs the dashboard uses once the CAN database is settled. */
  if (HAL_FDCAN_ConfigGlobalFilter(hfdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                   FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK)
  {
    return false;
  }
  if (HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0U) != HAL_OK)
  {
    return false;
  }
  return HAL_FDCAN_Start(hfdcan) == HAL_OK;
}

bool can_bus_init(FDCAN_HandleTypeDef *bus1, FDCAN_HandleTypeDef *bus2)
{
  bool ok = true;

  memset(s_bus, 0, sizeof(s_bus));
  s_bus[CAN_BUS_1].hfdcan = bus1;
  s_bus[CAN_BUS_2].hfdcan = bus2;

  for (uint32_t i = 0U; i < CAN_BUS_COUNT; i++)
  {
    ok = start_bus(s_bus[i].hfdcan) && ok;
  }
  return ok;
}

/* Called by HAL_FDCAN_IRQHandler() from FDCANx_IT0_IRQHandler(). Drains the
   hardware FIFO into the bus's queue. */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  bus_state_t *bus = find_bus(hfdcan);
  FDCAN_RxHeaderTypeDef header;
  uint8_t data[RX_SCRATCH_LEN];

  if ((bus == NULL) || ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U))
  {
    return;
  }

  while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0U)
  {
    uint32_t head = bus->head;
    uint32_t next = (head + 1U) & (RX_QUEUE_LEN - 1U);
    can_frame_t *frame;
    uint8_t len;

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, data) != HAL_OK)
    {
      break;
    }

    bus->last_rx_tick = HAL_GetTick();
    bus->seen = true;

    if (next == bus->tail)
    {
      bus->dropped++;   /* queue full: keep the older frames */
      continue;
    }

    /* Classic CAN: FDCAN_DLC_BYTES_0..8 are the byte counts 0..8. */
    len = (header.DataLength > 8U) ? 8U : (uint8_t)header.DataLength;

    frame = &bus->queue[head];
    frame->id = header.Identifier;
    frame->extended = (header.IdType == FDCAN_EXTENDED_ID);
    frame->len = len;
    memcpy(frame->data, data, len);
    frame->rx_tick = bus->last_rx_tick;

    __DMB();            /* frame contents before the new head */
    bus->head = next;
  }
}

bool can_bus_receive(can_bus_t bus, can_frame_t *frame)
{
  bus_state_t *b;
  uint32_t tail;

  if (bus >= CAN_BUS_COUNT)
  {
    return false;
  }
  b = &s_bus[bus];
  tail = b->tail;
  if (tail == b->head)
  {
    return false;
  }

  *frame = b->queue[tail];
  __DMB();              /* finish the copy before freeing the slot */
  b->tail = (tail + 1U) & (RX_QUEUE_LEN - 1U);
  return true;
}

bool can_bus_send(can_bus_t bus, uint32_t id, bool extended, const uint8_t *data, uint8_t len)
{
  FDCAN_HandleTypeDef *hfdcan;
  FDCAN_TxHeaderTypeDef header = {0};

  if ((bus >= CAN_BUS_COUNT) || (len > 8U) || ((data == NULL) && (len > 0U)))
  {
    return false;
  }
  if (id > (extended ? 0x1FFFFFFFU : 0x7FFU))
  {
    return false;
  }
  hfdcan = s_bus[bus].hfdcan;
  if (is_bus_off(hfdcan) || (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U))
  {
    return false;
  }

  header.Identifier = id;
  header.IdType = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = len;          /* FDCAN_DLC_BYTES_0..8 */
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0U;

  return HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &header, data) == HAL_OK;
}

bool can_bus_is_active(can_bus_t bus, uint32_t timeout_ms)
{
  const bus_state_t *b;

  if (bus >= CAN_BUS_COUNT)
  {
    return false;
  }
  b = &s_bus[bus];
  if (!b->seen || is_bus_off(b->hfdcan))
  {
    return false;
  }
  return (HAL_GetTick() - b->last_rx_tick) <= timeout_ms;
}

uint32_t can_bus_dropped(can_bus_t bus)
{
  return (bus < CAN_BUS_COUNT) ? s_bus[bus].dropped : 0U;
}

void can_bus_service(void)
{
  for (uint32_t i = 0U; i < CAN_BUS_COUNT; i++)
  {
    FDCAN_HandleTypeDef *hfdcan = s_bus[i].hfdcan;

    /* Bus-off sets CCCR.INIT and stops the controller. Clearing INIT starts
       the standard recovery: the controller rejoins after it has seen 128
       runs of 11 recessive bits on the bus. */
    if ((hfdcan != NULL) && is_bus_off(hfdcan))
    {
      CLEAR_BIT(hfdcan->Instance->CCCR, FDCAN_CCCR_INIT);
    }
  }
}
