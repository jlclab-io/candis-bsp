# Candis API guide

[Documentation](README.md) · [简体中文](../zh/api.md)

| Peripheral | Header | Entry points |
| --- | --- | --- |
| Main I²C | [`bsp/candis.h`](../../components/candis/include/bsp/candis.h) | `candis_i2c_acquire()` / `candis_i2c_release()` |
| AMOLED and touch | [`bsp/esp-bsp.h`](../../components/candis/include/bsp/esp-bsp.h) | `candis_display_start()` / `candis_display_stop()` |
| Power | [`bsp/candis_power.h`](../../components/candis/include/bsp/candis_power.h) | `candis_power_read()` |
| RTC | [`bsp/candis_rtc.h`](../../components/candis/include/bsp/candis_rtc.h) | `candis_rtc_get_time()` / `candis_rtc_set_time()` |
| TF card | [`bsp/candis_storage.h`](../../components/candis/include/bsp/candis_storage.h) | `candis_storage_mount()` / `candis_storage_unmount()` |
| IMU | [`bsp/candis_imu.h`](../../components/candis/include/bsp/candis_imu.h) | `candis_imu_start()` / `candis_imu_stop()` |
| Audio | [`bsp/candis_audio.h`](../../components/candis/include/bsp/candis_audio.h) | `candis_audio_open()` / `candis_audio_close()` |
| Baseboard | [`bsp/candis_baseboard.h`](../../components/candis/include/bsp/candis_baseboard.h) | `candis_baseboard_probe()` |

## Display and touch

Enable PSRAM and LVGL software drawing. Confirm [panel power](hardware.md#power) before startup. Use `bsp_display_start()` for default settings or `candis_display_start()` for an explicit configuration and error result.

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

## I²C and power ownership

Use `candis_i2c_acquire()` to share the main I²C bus on GPIO6/7. Remove application-owned devices before releasing the matching reference. Release all HP I²C users before switching these pins to LP I²C. Leave the PMU under BSP ownership.

Use `candis_power_read()` to query rail enable states. Storage and IMU share DC1SW, with an optional application reference via `candis_expansion_power_acquire()` and `candis_expansion_power_release()`. Use the existing supply by default. Enable rail management only when the application owns the complete power domain. Stop dependent peripherals before releasing the application reference. The BSP restores only the power settings it changed after the last corresponding owner releases them.

## RTC

`candis_rtc_get_time()` reads UTC in the years 2000–2099. A stopped or invalid calendar returns `ESP_ERR_INVALID_STATE`. Set the time with `candis_rtc_set_time(&utc, false)`. Use `true` only to recover an invalid calendar. Recovery resets inactive RTC settings and is rejected while backup charging is enabled.

Use `candis_rtc_arm_update_irq()` and `candis_rtc_disarm_update_irq()` to bracket RTC update wake. Store the zero-initialized state in retained memory for deep sleep. The application configures SoC wake and coordinates both devices on the shared GPIO0 interrupt. Preserve state and retry cleanup when `restore_pending` is set. See the [RTC example](examples.md#rtc-console).

## Storage and motion

Select the storage profile for the card-detect wiring on your hardware. Use `candis_storage_mount_with_config()` with `read_only = true` for browsing. Mounting leaves automatic formatting disabled. Applications can poll `candis_storage_detect_read()` with their own debounce after `candis_storage_detect_start()`. Close files and unmount before stopping card detection and removing power.

Enable `CONFIG_CANDIS_IMU` to sample acceleration and angular velocity. The sample includes a 24-bit sensor counter in 39.0625 µs ticks and the host read-completion time in microseconds. Apply 24-bit wraparound when comparing sensor ticks. Use `candis_imu_read_temperature()` for the sensor's internal temperature.

## Audio

Enable `CONFIG_CANDIS_AUDIO` for ES8389 and I²S. The stream format is 48 kHz, signed 16-bit interleaved stereo PCM. Start with the amplifier off, send silence, and set the volume before enabling output. The read and write functions report the number of frames transferred. Stop I/O before closing the codec and releasing its power reference.

## Expansion and camera

- Release UART0 and confirm DC1SW before calling `candis_baseboard_probe()`. Use the returned identity with the [Dock](../../components/candis_dock/README.md) or [Gamepad](../../components/candis_gamepad/README.md) API. See [current BSP support](expansion.md#bsp-support) for descriptor compatibility.
- The [camera API](../../components/candis-camera/README.md) accepts board-specific pins, SCCB, clock and power callbacks. Choose the OV5640 mode at build time. Copy a still frame into application memory, or acquire and release DMA frames for streaming.
