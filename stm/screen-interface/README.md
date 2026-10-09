# screen-interface

ILI9341 2.4" 240x320 SPI TFT driver and demo for the NUCLEO-G474RE (STM32 HAL, CubeMX + CMake project).

## ILI9341 display

### Wiring

| Display pin | Arduino header | STM32 pin | Function |
|-------------|----------------|-----------|----------|
| VCC         | 3V3            | -         | 3.3 V supply |
| GND         | GND            | -         | Ground |
| CS          | D10            | PB6       | GPIO output `TFT_CS`, software chip select (active low) |
| RESET       | D8             | PA9       | GPIO output `TFT_RST` (active low) |
| DC          | D9             | PC7       | GPIO output `TFT_DC` (low = command, high = data) |
| SDI (MOSI)  | D11            | PA7       | SPI1_MOSI |
| SCK         | D13            | PA5       | SPI1_SCK |
| LED         | 3V3            | -         | Backlight always on (or D7 / PA8 = TIM1_CH1 for PWM dimming, not configured) |
| SDO (MISO)  | not connected  | -         | D12 / PA6 when read-back or touch is added |
| T_CLK, T_CS, T_DIN, T_DO, T_IRQ | not connected | - | XPT2046 touch, not used yet |

PA5 is also the green LD2 LED. LD2 is disabled in the board (BSP) settings of the `.ioc`, because `BSP_LED_Init()` would turn PA5 back into a plain GPIO and stop the SPI clock. The LED flickers while the display is being written; do not call `BSP_LED_*` in this project.

### SPI settings

| Setting | Value |
|---------|-------|
| Peripheral | SPI1, Transmit Only Master |
| Frame | 8 bit, MSB first |
| Mode | 0 (CPOL low, CPHA 1 edge) |
| NSS | Software (`TFT_CS` GPIO) |
| Clock | PCLK2 170 MHz / 16 = 10.6 MHz |
| Pins | PA5 and PA7 alternate function push-pull, Very High speed; CS, DC, RST output push-pull, Very High speed, initially high |

To change the speed, set `ILI9341_SPI_PRESCALER` in `Core/Inc/ili9341.h` (or define it on the compiler command line). The driver writes this prescaler to SPI1 each time it takes the bus, so it overrides the prescaler chosen in CubeMX.

### Driver options (`Core/Inc/ili9341.h`)

| Macro | Default | Purpose |
|-------|---------|---------|
| `ILI9341_SPI_PRESCALER` | `SPI_BAUDRATEPRESCALER_16` | SPI clock divider |
| `ILI9341_PANEL_BGR` | `1` | Set to `0` if red and blue are swapped |
| `ILI9341_USE_DMA` | `0` | Send large pixel blocks with `HAL_SPI_Transmit_DMA` |
| `ILI9341_BUF_SIZE` | `512` | Static buffer for fills and text, in bytes |

For `ILI9341_USE_DMA=1`, in CubeMX open SPI1 > DMA Settings, add `SPI1_TX` (Normal mode, memory increment, byte width) and keep the DMA channel global interrupt enabled, then regenerate.

### Build and flash

The project uses CMake presets with the STM32Cube toolchain bundle. In VS Code with the STM32Cube extension, pick the `Debug` preset, build, and use Run and Debug to flash.

From a terminal, with `arm-none-eabi-gcc`, `cmake` and `ninja` on `PATH`:

```sh
cmake --preset Debug
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/screen-interface.elf -v -rst
```

### Demo

On reset the screen fills red, green, then blue for 500 ms each, then shows a white border, "Hello, STM32G474", four swatches labelled R G B Y, and a counter that goes up once per second.

### Troubleshooting

| Symptom | Things to check |
|---------|-----------------|
| White screen (backlight only) | The controller never got a valid init. Check CS (D10), DC (D9), RESET (D8), SCK (D13) and MOSI (D11) wiring and that VCC is 3.3 V. Confirm `BSP_LED_Init()` is not called anywhere (it disables SCK on PA5). Try a slower clock (`SPI_BAUDRATEPRESCALER_64`). Some modules have a J1 jumper for 3.3 V supply. |
| Screen stays dark | The LED pin is not connected to 3V3. |
| Red and blue swapped | Set `ILI9341_PANEL_BGR` to `0`. |
| Colours inverted (black shows as white) | The panel needs display inversion: send command `0x21` after init. |
| Mirrored image | The panel is mounted the other way round: toggle `MADCTL_MX` (or `MADCTL_MY`) in the four cases of `ili9341_set_rotation()`. |
| Rotated image | Call `ili9341_set_rotation()` with 0 to 3. |
| Garbled or shifted output, random pixels | Signal integrity: shorten the wires, add a ground wire next to SCK, lower the SPI clock. Check that the mode is 0 and the frame is 8 bit. A DC wire that is loose gives random colours with the right shapes. |
| Works after flashing but not after power-up | RESET is not connected, or the supply ramps slowly. Keep RESET wired to D8. |
| Slow updates | Raise the SPI clock (`SPI_BAUDRATEPRESCALER_8` or `_4`; the datasheet only guarantees a 100 ns write cycle, i.e. 10 MHz, but most modules run faster on short wires, so test it), build the `Release` preset, enable `ILI9341_USE_DMA`, and redraw only what changed instead of the full screen. A full-screen fill is 153600 bytes, about 120 ms at 10.6 MHz. |

### Adding touch later

The XPT2046 shares SPI1 and needs its own chip select (T_CS -> D6 / PB10), T_IRQ (-> D5 / PB4), MISO (T_DO and SDO -> D12 / PA6) and a clock of about 2 MHz. SPI1 must be changed to Full-Duplex Master in CubeMX. The touch driver should set its own prescaler before each transfer in the same way as `bus_begin()` in `ili9341.c`; the display driver restores its prescaler when it next takes the bus. Keep T_CS high whenever the display is being written.

For LVGL, the flush callback is `ili9341_set_window()` followed by `ili9341_write_pixels()` with the buffer in RGB565, high byte first (`LV_COLOR_16_SWAP` or `lv_draw_sw_rgb565_swap()`).
