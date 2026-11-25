# Candis

Board support for Candis revision 1.2, an ESP32-S31 development board with a 480 × 480 AMOLED and CST9220 touch. ESP-IDF 6.2 is the primary environment and 6.1 is supported for compatibility. The component works with the core board on its own. It natively depends on `tg28-esp`; that driver can also be consumed directly by applications that do not use the Candis BSP.

## Use in an application

Add the `candis` component to the application manifest. See the [repository quick start](https://github.com/jlclab-io/candis-bsp/blob/main/README.md#quick-start) for local and Git dependencies.

Include [`bsp/esp-bsp.h`](include/bsp/esp-bsp.h) and use `BSP_CAPS_*` for build capabilities. Supply ALDO1 and enable PSRAM before calling `bsp_display_start()`, then enable brightness. Protect LVGL calls with `bsp_display_lock()` and `bsp_display_unlock()`. Set `CONFIG_CANDIS_NO_GRAPHIC_LIB=y` for native panel and touch without LVGL.

Use `bsp_i2c_get_handle()` for the fixed core-board I²C bus. For storage, `candis_storage_configure()` supplies the application's SDMMC pins, card-detect polarity, and optional DC1SW policy before `bsp_sdcard_mount()` or `candis_storage_mount()`. Motion uses a caller-owned bus, address, and ranges in `candis_imu_config_t`. The motor API likewise takes caller-selected GPIO, LEDC resources, and optional supply. `candis-dock` owns its known Dock wiring, while other baseboards and application-specific assemblies keep their assignments in the application.

The component also provides read-only PMU status and the RX8130CE calendar. Enable the corresponding Kconfig options for ES8389 audio, BMI270 motion, or native USB Device and Host. Confirm the attached hardware and supplies before first use.

The component follows the ESP-BSP layout. Application headers are under `include/bsp`, implementation files are under `src`, and internal board and resource helpers are under `priv_include`. Only the public headers and `REQUIRES` dependencies are exported to consumers.

[API guide](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/api.md) · [Hardware](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/hardware.md) · [Examples](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/examples.md) · [Apache-2.0](LICENSE)
