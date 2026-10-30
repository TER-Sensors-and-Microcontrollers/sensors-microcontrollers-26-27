/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
#include "lcd_port.h"
#include "dash_ui.h"
#include "dash_mock.h"
#include "can_bus.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* A CAN bus counts as alive if it received a frame this recently. */
#define CAN_ACTIVE_TIMEOUT_MS   500U

/* Longest the main loop sleeps, so received CAN frames are handled promptly
   and the software queues (can_bus.c) do not fill up. */
#define MAIN_LOOP_MAX_IDLE_MS   5U

#define CAN_NOTIFICATION_ID     0x123U

/* BNO055 IMU frames, as sent by stm/can-frame-test. Standard IDs; each
   payload is three little-endian int16 raw BNO055 counts:
     0x100 accel x, y, z           100 LSB per m/s^2   (6 bytes)
     0x101 gyro  x, y, z            16 LSB per deg/s   (6 bytes)
     0x102 mag   x, y, z            16 LSB per uT      (6 bytes)
     0x103 heading, roll, pitch     16 LSB per degree, byte 6 = CALIB_STAT
           (sys | gyro | accel | mag, 2 bits each), byte 7 = 7-bit I2C
           address the IMU answered at                 (8 bytes)
   The IDs are consecutive and in dash_imu_t order. A 7-byte 0x103 (older
   sender firmware, no address) is still decoded; the address shows "0x--". */
#define CAN_ID_IMU_FIRST        0x100U
#define IMU_CALIB_FRAME_LEN     7U
#define IMU_ADDR_FRAME_LEN      8U
#define IMU_FRAME_LEN           6U

/* An IMU row shows "--" if its frame is older than this. */
#define IMU_TIMEOUT_MS          500U

/* The IMU sends at 100 Hz; redraw its readout at 10 Hz so the numbers can
   be read and the SPI link is not kept busy. */
#define IMU_UI_PERIOD_MS        100U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
FDCAN_HandleTypeDef hfdcan1;
FDCAN_HandleTypeDef hfdcan3;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

/* USER CODE BEGIN PV */
/* Main display (speed, state of charge, status, IMU) on SPI1. */
static const lcd_port_t s_lcd_main = {
  .hspi = &hspi1,
  .cs_port = TFT_CS_GPIO_Port,   .cs_pin = TFT_CS_Pin,
  .dc_port = TFT_DC_GPIO_Port,   .dc_pin = TFT_DC_Pin,
  .rst_port = TFT_RST_GPIO_Port, .rst_pin = TFT_RST_Pin,
};

/* Second display (temperatures, CAN status) on SPI2. */
static const lcd_port_t s_lcd_aux = {
  .hspi = &hspi2,
  .cs_port = TFT2_CS_GPIO_Port,   .cs_pin = TFT2_CS_Pin,
  .dc_port = TFT2_DC_GPIO_Port,   .dc_pin = TFT2_DC_Pin,
  .rst_port = TFT2_RST_GPIO_Port, .rst_pin = TFT2_RST_Pin,
};

/* Latest IMU frame of each kind, raw counts. */
typedef struct
{
  int16_t  raw[3];
  uint32_t rx_tick;
  bool     seen;
} imu_sample_t;

static imu_sample_t s_imu[DASH_IMU_COUNT];
static uint8_t s_imu_calib_stat;
static int32_t s_imu_addr = DASH_NO_DATA;  /* 7-bit I2C address, from 0x103 */

/* Raw counts per unit, in dash_imu_t order. */
static const int32_t k_imu_lsb_per_unit[DASH_IMU_COUNT] = {100, 16, 16, 16};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_FDCAN3_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */
static void gui_init(void);
static void can_poll(void);
static void handle_can_frame(can_bus_t bus, const can_frame_t *frame);
static bool handle_imu_frame(const can_frame_t *frame);
static void imu_show(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_FDCAN1_Init();
  MX_FDCAN3_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */
  gui_init();

  if (!can_bus_init(&hfdcan1, &hfdcan3))
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    can_poll();

    /* Runs LVGL's timers: refresh of both screens and the dashboard's mock
       data. Sleep until the next one is due, but not so long that CAN frames
       pile up. */
    uint32_t idle_ms = lv_timer_handler();

    if ((idle_ms == LV_NO_TIMER_READY) || (idle_ms > MAIN_LOOP_MAX_IDLE_MS))
    {
      idle_ms = MAIN_LOOP_MAX_IDLE_MS;
    }
    HAL_Delay(idle_ms);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 17;
  hfdcan1.Init.NominalSyncJumpWidth = 4;
  hfdcan1.Init.NominalTimeSeg1 = 15;
  hfdcan1.Init.NominalTimeSeg2 = 4;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief FDCAN3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN3_Init(void)
{

  /* USER CODE BEGIN FDCAN3_Init 0 */

  /* USER CODE END FDCAN3_Init 0 */

  /* USER CODE BEGIN FDCAN3_Init 1 */

  /* USER CODE END FDCAN3_Init 1 */
  hfdcan3.Instance = FDCAN3;
  hfdcan3.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan3.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan3.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan3.Init.AutoRetransmission = DISABLE;
  hfdcan3.Init.TransmitPause = DISABLE;
  hfdcan3.Init.ProtocolException = DISABLE;
  hfdcan3.Init.NominalPrescaler = 17;
  hfdcan3.Init.NominalSyncJumpWidth = 4;
  hfdcan3.Init.NominalTimeSeg1 = 15;
  hfdcan3.Init.NominalTimeSeg2 = 4;
  hfdcan3.Init.DataPrescaler = 1;
  hfdcan3.Init.DataSyncJumpWidth = 1;
  hfdcan3.Init.DataTimeSeg1 = 1;
  hfdcan3.Init.DataTimeSeg2 = 1;
  hfdcan3.Init.StdFiltersNbr = 0;
  hfdcan3.Init.ExtFiltersNbr = 0;
  hfdcan3.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN3_Init 2 */

  /* USER CODE END FDCAN3_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, TFT2_RST_Pin|TFT2_DC_Pin|TFT2_CS_Pin|TFT_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : TFT2_RST_Pin TFT2_DC_Pin TFT2_CS_Pin TFT_CS_Pin */
  GPIO_InitStruct.Pin = TFT2_RST_Pin|TFT2_DC_Pin|TFT2_CS_Pin|TFT_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_DC_Pin */
  GPIO_InitStruct.Pin = TFT_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TFT_DC_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_RST_Pin */
  GPIO_InitStruct.Pin = TFT_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(TFT_RST_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* LVGL porting steps: init, tick source, display drivers, then the UI. */
static void gui_init(void)
{
  lv_display_t *main_disp;
  lv_display_t *aux_disp;

  lv_init();
  lv_tick_set_cb(HAL_GetTick);   /* SysTick already counts milliseconds */
  lv_delay_set_cb(HAL_Delay);    /* used by the ILI9341 init sequence */

  main_disp = lcd_port_init(&s_lcd_main);
  aux_disp = lcd_port_init(&s_lcd_aux);
  if ((main_disp == NULL) || (aux_disp == NULL))
  {
    Error_Handler();
  }

  dash_ui_create(main_disp, aux_disp);
  dash_mock_start();
}

/* Handle every CAN frame received since the last pass and show which buses
   are alive. Runs in the main loop, so it may call the dash_ui setters. */
static void can_poll(void)
{
  static uint32_t rx_count;   /* frames received on both buses since reset */
  can_frame_t frame;

  can_bus_service();

  for (can_bus_t bus = CAN_BUS_1; bus < CAN_BUS_COUNT; bus++)
  {
    while (can_bus_receive(bus, &frame))
    {
      handle_can_frame(bus, &frame);
      rx_count++;
    }
  }

  dash_ui_set_can_rx_count(rx_count);
  imu_show();

  dash_ui_set_can_status(can_bus_is_active(CAN_BUS_1, CAN_ACTIVE_TIMEOUT_MS),
                         can_bus_is_active(CAN_BUS_2, CAN_ACTIVE_TIMEOUT_MS));
}

/* Decode the car's frames here and pass the values to the dash_ui_set_*()
   setters, then remove dash_mock_start() from gui_init(). */
static void handle_can_frame(can_bus_t bus, const can_frame_t *frame)
{
  (void)bus;

  if (handle_imu_frame(frame))
  {
    return;
  }

  if (!frame->extended && (frame->id == CAN_NOTIFICATION_ID) &&
      (frame->len == 8U) &&
      (frame->data[0] == 0xFFU) &&
      (frame->data[1] == 0x00U) &&
      (frame->data[2] == 0xFFU) &&
      (frame->data[3] == 0x00U) &&
      (frame->data[4] == 0xFFU) &&
      (frame->data[5] == 0x00U) &&
      (frame->data[6] == 0xFFU) &&
      (frame->data[7] == 0x00U))
  {
    dash_ui_show_can_notification();
  }
}

/* Store an IMU frame (IDs 0x100..0x103, from either bus). Returns false if
   `frame` is not one. */
static bool handle_imu_frame(const can_frame_t *frame)
{
  uint32_t row;
  imu_sample_t *s;

  if (frame->extended || (frame->id < CAN_ID_IMU_FIRST) ||
      (frame->id >= (CAN_ID_IMU_FIRST + (uint32_t)DASH_IMU_COUNT)))
  {
    return false;
  }
  row = frame->id - CAN_ID_IMU_FIRST;
  if (frame->len < ((row == (uint32_t)DASH_IMU_EULER) ? IMU_CALIB_FRAME_LEN : IMU_FRAME_LEN))
  {
    return true;  /* an IMU ID, but too short to decode */
  }

  s = &s_imu[row];
  for (uint32_t i = 0U; i < 3U; i++)
  {
    s->raw[i] = (int16_t)((uint16_t)frame->data[2U * i] | ((uint16_t)frame->data[(2U * i) + 1U] << 8));
  }
  if (row == (uint32_t)DASH_IMU_EULER)
  {
    s_imu_calib_stat = frame->data[6];
    /* The sender uses 0 for "no IMU". */
    s_imu_addr = ((frame->len >= IMU_ADDR_FRAME_LEN) && (frame->data[7] != 0U))
                 ? (int32_t)frame->data[7] : DASH_NO_DATA;
  }
  s->rx_tick = frame->rx_tick;
  s->seen = true;
  return true;
}

/* Raw counts to tenths of a unit, rounded to nearest. */
static int32_t imu_to_tenths(int16_t raw, int32_t lsb_per_unit)
{
  const int32_t n = (int32_t)raw * 10;

  return (n >= 0) ? ((n + (lsb_per_unit / 2)) / lsb_per_unit)
                  : -((-n + (lsb_per_unit / 2)) / lsb_per_unit);
}

/* Update the IMU readout every IMU_UI_PERIOD_MS. A row whose frame stopped
   arriving shows "--". */
static void imu_show(void)
{
  static uint32_t last_ms;
  const uint32_t now = HAL_GetTick();

  if ((now - last_ms) < IMU_UI_PERIOD_MS)
  {
    return;
  }
  last_ms = now;

  for (uint32_t row = 0U; row < (uint32_t)DASH_IMU_COUNT; row++)
  {
    const imu_sample_t *s = &s_imu[row];
    const int32_t lsb = k_imu_lsb_per_unit[row];

    if (s->seen && ((now - s->rx_tick) < IMU_TIMEOUT_MS))
    {
      dash_ui_set_imu((dash_imu_t)row, imu_to_tenths(s->raw[0], lsb),
                      imu_to_tenths(s->raw[1], lsb), imu_to_tenths(s->raw[2], lsb));
    }
    else
    {
      dash_ui_set_imu((dash_imu_t)row, DASH_NO_DATA, DASH_NO_DATA, DASH_NO_DATA);
    }
  }

  if (s_imu[DASH_IMU_EULER].seen && ((now - s_imu[DASH_IMU_EULER].rx_tick) < IMU_TIMEOUT_MS))
  {
    dash_ui_set_imu_calib((int32_t)((s_imu_calib_stat >> 6) & 0x3U));  /* system level */
    dash_ui_set_imu_addr(s_imu_addr);
  }
  else
  {
    dash_ui_set_imu_calib(DASH_NO_DATA);
    dash_ui_set_imu_addr(DASH_NO_DATA);
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
