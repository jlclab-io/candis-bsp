# Candis

Board support for Candis revision 1.2, an ESP32-S31 development board with a 480 × 480 AMOLED and CST9220 touch. Requires ESP-IDF 6.2 and works with the core board on its own.

## Use in an application

Add the `candis` component to the application manifest. See the [repository quick start](https://github.com/jlclab-io/candis-bsp/blob/main/README.md#quick-start) for local and Git dependencies.

Include [`bsp/esp-bsp.h`](include/bsp/esp-bsp.h) and use `BSP_CAPS_*` for build capabilities. Supply ALDO1 and enable PSRAM before calling `bsp_display_start()`, then enable brightness. Protect LVGL calls with `bsp_display_lock()` and `bsp_display_unlock()`. Set `CONFIG_CANDIS_NO_GRAPHIC_LIB=y` for native panel and touch without LVGL.

Use `bsp_i2c_get_handle()` for shared I²C and `bsp_sdcard_mount()` for the Dock TF slot. The BSP also provides read-only PMU status, bounded PWM for a fitted M1 motor, and the RX8130CE calendar. Enable the corresponding Kconfig options for ES8389 audio, BMI270 motion, or native USB Device and Host. Select the board profile and confirm its supplies before first use.

The component follows the ESP-BSP layout. Application headers are under `include/bsp`, implementation files are under `src`, and internal board and resource helpers are under `priv_include`. Only the public headers and `REQUIRES` dependencies are exported to consumers.

[API guide](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/api.md) · [Hardware](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/hardware.md) · [Examples](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/examples.md) · [Apache-2.0](LICENSE)
