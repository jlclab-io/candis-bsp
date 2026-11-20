# Candis

Board support for Candis revision 1.2, an ESP32-S31 development board with a 480 × 480 AMOLED and CST9220 touch. Requires ESP-IDF 6.2 and works with the core board on its own.

## Use in an application

Add the `candis` component to the application manifest. See the [repository quick start](https://github.com/jlclab-io/candis-bsp/blob/main/README.md#quick-start) for local and Git dependencies.

Include [`bsp/esp-bsp.h`](include/bsp/esp-bsp.h) and start the display with `bsp_display_start()` or `candis_display_start()`. Supply ALDO1 and enable PSRAM. Protect LVGL operations with `bsp_display_lock()` and `bsp_display_unlock()`. The default asynchronous refresh can be changed through `candis_display_config_t`.

The BSP also exposes shared main I²C, power state, RX8130CE UTC calendar, SDMMC mounting, and optional ES8389 audio and BMI270 motion sensing. Choose the appropriate board profile before enabling expansion functions.

[API guide](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/api.md) · [Hardware](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/hardware.md) · [Examples](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/examples.md) · [Apache-2.0](LICENSE)
