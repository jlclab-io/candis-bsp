# Candis API guide

[Documentation](README.md) · [简体中文](../zh/api.md)

Use [bsp/esp-bsp.h](../../components/candis/include/bsp/esp-bsp.h) from application code. The `BSP_CAPS_*` macros describe the interfaces supplied by the current build. Enable `CONFIG_CANDIS_AUDIO`, `CONFIG_CANDIS_IMU`, `CONFIG_CANDIS_USB_DEVICE` or `CONFIG_CANDIS_USB_HOST` to include those drivers. Confirm the attached hardware and its supplies before starting each interface.

The common interfaces follow the [ESP-BSP S31 reference](https://github.com/espressif/esp-bsp/tree/fb5714e02cf4289d6f092f817a7ef1e0b72a49ed/bsp/esp32_s31_korvo_1). Candis uses `candis_*` for board-specific capabilities.

| Peripheral | Start and use | Release |
| --- | --- | --- |
| Shared I²C | `bsp_i2c_init()` / `bsp_i2c_get_handle()` | `bsp_i2c_deinit()` |
| AMOLED and LVGL touch | `bsp_display_start()` / `bsp_display_start_with_config()` / `bsp_display_get_input_dev()` | `candis_display_stop()` or `bsp_display_stop()` |
| Native panel and touch | `bsp_display_new()` / `bsp_touch_new()` | `esp_lcd_panel_del()` / `bsp_touch_del()` |
| TF card | `candis_storage_configure()` then `bsp_sdcard_mount()` or `candis_storage_mount_with_config()` | `bsp_sdcard_unmount()` or `candis_storage_unmount()` |
| ES8389 audio | `bsp_audio_codec_speaker_init()` / `bsp_audio_codec_microphone_init()` | `bsp_audio_deinit()` |
| BMI270 | `candis_imu_start_with_config()` / `candis_imu_read()` | `candis_imu_stop()` |
| OTG CDC Device / Host | `candis_usb_device_start()` / `bsp_usb_host_start()` | `candis_usb_device_stop()` / `bsp_usb_host_stop()` |
| Power | `candis_power_read()` / `candis_power_read_supply()` / `candis_power_read_battery_uv()` | Read-only calls release their own I²C reference |
| Motor | `candis_motor_init()` / `candis_motor_start_amplitude()` / `candis_motor_set_amplitude()` / `candis_motor_set_frequency()` / `candis_motor_get_state()` | `candis_motor_stop()`; bounded pulses also release automatically |
| RTC and baseboard | `candis_rtc_get_time()` / `candis_baseboard_probe_on_bus()` | One-shot access |
| Optional baseboard state | `candis_baseboard_probe_state_on_bus()` / `candis_dock_get_state()` | Returns unsupported, not present, present, or initialized |

## ESP-BSP compatibility

| Reference surface | Candis status |
| --- | --- |
| `bsp_display_new()` | Implemented, including `bsp_display_new_with_handles()` and `bsp_display_delete()` |
| `bsp_display_start*()`, lock, rotation, brightness | Implemented. `bsp_display_enter_sleep()` and `bsp_display_exit_sleep()` also suspend and resume the panel and CST9220 without releasing the resources |
| `bsp_touch_new()` | Implemented for CST9220, with configurable axis exchange and mirroring |
| `bsp_i2c_init/get_handle/deinit` | One idempotent reference for the standard calls |
| `bsp_audio_init/get_codec_itf/codec_*_init` | One duplex session when audio is enabled |
| `bsp_sdcard_mount()` and SDMMC host/slot helpers | Implemented for the application-configured SDMMC slot. `bsp_sdcard_sdspi_mount()` returns `ESP_ERR_NOT_SUPPORTED` |
| `bsp_usb_host_start/stop` | Controls the Host data role. The Type-C circuit controls VBUS and current limiting |
| SPIFFS and the Korvo-specific ADC, LED, button and camera APIs | Application-specific partitions and native drivers. Camera has a separate component |

`BSP_CAPS_*` is a compile-time declaration for the Candis core board and enabled build options. `BSP_CAPS_SDCARD` and `BSP_CAPS_MOTOR` remain zero because the transport and output are application-defined. A baseboard descriptor is decoded after the application supplies its bus and address. The [Dock component](../../components/candis-dock/README.md) supplies the known Dock address and validates its revisions. `ESP_ERR_NOT_FOUND` means the probe did not find a descriptor, `ESP_ERR_INVALID_CRC` means the descriptor is unusable, and `ESP_ERR_NOT_SUPPORTED` means the descriptor is outside the supported format or identity. Component open calls return `ESP_ERR_INVALID_STATE` when their required power or initialization ownership is not in place.

Use the state APIs after powering the expansion domain and releasing UART0. `PRESENT` means the expected descriptor is valid and the component has not been opened. `INITIALIZED` means that component owns its active session. A transport or power error is returned separately from the state value.

Lifecycle calls run in task context and may return `ESP_ERR_INVALID_STATE` during contention. Stop I/O and callbacks, then call the matching release function. Retry it if cleanup fails. Display control takes the board lock before the LVGL lock, then waits for DMA. Call display lifecycle functions outside the LVGL lock and callbacks.

## Display and touch

Enable PSRAM and LVGL software drawing. Confirm [panel power](hardware.md#power) before startup. After `bsp_display_start()` or `bsp_display_start_with_config()`, set brightness or call `bsp_display_backlight_on()`. Use `candis_display_start()` for an explicit error result and a configured initial brightness.

```c
#include "bsp/esp-bsp.h"

void app_main(void)
{
    const candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    lv_display_t *display = NULL;
    ESP_ERROR_CHECK(candis_display_start(&config, &display));
    ESP_ERROR_CHECK(bsp_display_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT);

    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(display));
    lv_label_set_text(label, "Hello, Candis!");
    lv_obj_center(label);
    bsp_display_unlock();
}
```

Protect LVGL access with `bsp_display_lock()` and `bsp_display_unlock()`. Read up to two touch contacts and tracking IDs with `candis_display_get_touch_points()` while holding the lock. Set `CONFIG_ESP_LCD_TOUCH_MAX_POINTS=2` for two contacts.

Asynchronous refresh is enabled by default and uses an additional 450 KiB of PSRAM. Set `async_flush = false` for synchronous refresh. For a full-frame source such as a camera, use the separate panel-only mode with `candis_display_start_raw()` and `candis_display_draw_rgb565()`. The frame must be packed, native-endian 480 × 480 RGB565 and can be reused when the draw call returns.

Before stopping the display, cancel application callbacks and timers that use it. Call `candis_display_stop()` from a control task outside the LVGL lock.

Use `bsp_display_start_with_config()` to select the LVGL task or draw buffers. `bsp_display_rotate()` rotates RGB565 output in software. `bsp_display_backlight_on()` restores the last nonzero brightness.

Set `CONFIG_CANDIS_NO_GRAPHIC_LIB=y` to build the native panel and touch without LVGL or `esp_lvgl_port`. Set panel visibility via `esp_lcd_panel_disp_on_off()` and set brightness separately. Native RGB565 drawing is synchronous and requires even window edges. Read up to two native contacts with `candis_touch_read()`. The panel and standalone touch hold independent ALDO1 references. Delete their handles with `esp_lcd_panel_del()` or `esp_lcd_touch_del()`. If touch cleanup fails, retry with `bsp_touch_del(NULL)`.

Sleep waits for the panel task and DMA before suspending the display and its LVGL touch controller. Wake restores brightness and panel visibility. If a transition fails partway, retry it or release the display from a control task.

## I²C and power ownership

The standard I²C entry points share one idempotent reference. `candis_i2c_acquire()` remains available for separately counted application references. Remove each application I²C device before releasing the reference that owns it.

Use `candis_i2c_acquire()` to share the main I²C bus on GPIO6/7. Remove application-owned devices before releasing the matching reference. Release all HP I²C users before switching these pins to LP I²C. Leave the PMU under BSP ownership.

Dock, storage, and IMU use DC1SW only when their caller enables it. Storage and IMU do not select a baseboard pin or address. Use `candis_expansion_power_claim()/unclaim()` with an initially NULL handle for independent application references. The `candis_expansion_power_acquire()/release()` pair manages one application reference. The last release restores the rail settings changed by the BSP.

## RTC

`candis_rtc_get_time()` reads UTC in the years 2000–2099. A stopped or invalid calendar returns `ESP_ERR_INVALID_STATE`. Set the time with `candis_rtc_set_time(&utc, false)`. Use `true` only to recover an invalid calendar. Recovery resets inactive RTC settings and is rejected while backup charging is enabled.

Use `candis_rtc_arm_update_irq()` and `candis_rtc_disarm_update_irq()` to bracket RTC update wake. Store the zero-initialized state in retained memory for deep sleep. The application configures SoC wake and coordinates both devices on the shared GPIO0 interrupt. Preserve state and retry cleanup when `restore_pending` is set. See the [RTC example](examples.md#rtc-console).

## Storage and motion

The core component does not assign SDMMC pins or a card-detect GPIO. Pass an application-owned `candis_storage_config_t` to `candis_storage_configure()`. Set `use_dc1sw` only when the selected baseboard derives card power from DC1SW and the complete load is verified. The standard mount path defaults to `/sdcard` and is configurable via `CONFIG_BSP_SD_MOUNT_POINT`.

Select the profile for the actual card-detect wiring. Use `candis_storage_mount_with_config()` with `read_only = true` for browsing on ESP-IDF 6.2 or later. ESP-IDF 6.1 returns `ESP_ERR_NOT_SUPPORTED` for that request. Mounting leaves automatic formatting disabled. The storage owner polls `candis_storage_check_card()` and handles I/O failures. Once removal is observed, the mount reports `ESP_ERR_NOT_FOUND` through reinsertion until the application closes files and unmounts. Independent card detection uses `candis_storage_detect_read()` with application-side debounce.

Enable `CONFIG_CANDIS_IMU` to sample acceleration and angular velocity. Pass a powered, caller-owned bus and the application-selected BMI270 address to `candis_imu_start_with_config()`. Use `candis_imu_probe()` before starting when the address is optional. `candis_imu_suspend()/resume()` retain the caller's bus while switching advanced power saving. The first sample can return `ESP_ERR_NOT_FINISHED`. The 24-bit counter uses 39.0625 µs ticks. Use `candis_imu_read_temperature()` for die temperature.

## Audio

Enable `CONFIG_CANDIS_AUDIO` and supply ALDO2 before initialization. The speaker and microphone entry points borrow the same ES8389 device. Call `esp_codec_dev_open()` once with the stream format. The default I²S configuration uses 48 kHz, 16-bit stereo. The first speaker write sends silence before enabling the amplifier. Close the codec after stopping I/O, then call `bsp_audio_deinit()` to release it.

The Candis PCM API supports 48 kHz signed stereo with one RX task and one TX task concurrently. Call `candis_audio_set_mute()` to mute output and turn off the amplifier. Queue silence before enabling the amplifier again. The standard and Candis sessions are exclusive. The BSP owns the codec handle it lends to the application.

## USB and motor

- Enable `CONFIG_CANDIS_USB_DEVICE` for native USB CDC. The application uses nonblocking reads and writes and owns its own stdio routing.
- Enable `CONFIG_CANDIS_USB_HOST` to use the ESP USB Host client APIs. Stop Device CDC before starting Host. Use `BSP_USB_HOST_POWER_MODE_AUTO` and pass `false` for the programmable current limit. Type-C electronics manage VBUS independently. After clients deregister, `bsp_usb_host_stop()` stops the USB data stack and releases the PHY.
- Supply the motor GPIO, LEDC mode/channel/timer, carrier frequency, minimum and maximum PWM duty for the calibrated amplitude range, and optional expansion LDO in `candis_motor_config_t`, then call `candis_motor_init()`. `candis_motor_start_amplitude()` starts continuous vibration. `candis_motor_set_amplitude()` and `candis_motor_set_frequency()` can update an active output. Amplitude zero stops the output. Use `candis_motor_amplitude_to_duty()` or `candis_motor_duty_to_amplitude()` to inspect the configured mapping.
- `candis_motor_pulse()` and `candis_motor_pulse_amplitude()` produce bounded 1–600 ms pulses and restore a rail changed by the request. Query `candis_motor_get_state()` for the applied duty, amplitude, frequency and cleanup state, or call `candis_motor_stop()` to stop continuous output, cancel a pulse, or retry cleanup.

## Expansion and camera

- Create and power the application-selected expansion bus, then call `candis_baseboard_probe_on_bus()` or `candis_baseboard_probe_state_on_bus()`. The [Dock component](../../components/candis-dock/README.md) supplies its GPIO58/59 bus, address `0x50`, and known revision rules. See [current BSP support](expansion.md#bsp-support) for descriptor compatibility.
- The [camera API](../../components/candis-camera/README.md) takes application-defined pins, SCCB and XCLK settings, and resource callbacks. Select an Espressif DVP sensor and mode at build time. Copy a still frame into application memory or borrow and release DMA frames for streaming.
