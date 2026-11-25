# Candis Camera

Configurable 8-bit DVP capture for ESP32-S31 and ESP-IDF 6.1/6.2. The component uses [ESP Video 2.6.0](https://components.espressif.com/components/espressif/esp_video/versions/2.6.0) and [ESP Cam Sensor 2.7.0](https://components.espressif.com/components/espressif/esp_cam_sensor/versions/2.7.0).

## Sensor profiles

The component has one configuration structure per sensor in [src/profiles](src/profiles). `candis_camera_profile_get()` and `candis_camera_profile_at()` provide the reference mode, XCLK frequency, chip IDs, 7-bit SCCB addresses, and control capabilities. `CANDIS_CAMERA_PROFILE_BUILD_ENABLED` indicates that the official DVP detector is enabled in the firmware.

The profiles cover BF20A6, BF3031, BF3045, BF3925, BF3A03, GC0308, GC032A, GC2145, MT9D111, OV2640, OV3640, OV3660, OV5640, SC030IOT, SC101IOT, SC2336, and SP0A39. For BF3031, match `CONFIG_CAMERA_BF3031_SID_HIGH` to the module strap. SID low uses address `0x6e` and SID high uses `0x6d`.

## Application wiring

Fill `candis_camera_config_t` with the application's DVP pins, SCCB settings, and XCLK configuration. Use a dedicated HP I²C port or pass a borrowed bus with both SCCB pin fields set to `GPIO_NUM_NC`. Set `xclk` to `GPIO_NUM_NC` when the application or module supplies its own clock. Set `validate_sensor` and `expected_sensor` to verify the detected chip before capture. For a mode other than the profile reference mode, use the XCLK specified in the corresponding Espressif sensor format table.

The application owns supply voltages, power sequencing, RESET and PWDN. The optional `acquire` callback supplies board resources before XCLK starts. The optional `prepare` callback runs after XCLK starts and before detection. `release` runs after video and XCLK stop. Keep the callback context and borrowed bus valid until cleanup succeeds. Make `release` safe to retry after partial acquisition or cleanup. The [camera example](https://github.com/jlclab-io/candis-bsp/tree/main/examples/camera) shows an application-side resource adapter.

## Capture and controls

Enable DVP video and the sensor's DVP auto-detection option. Call the API in [bsp/candis_camera.h](include/bsp/candis_camera.h) from one control task.

| Operation | API |
| --- | --- |
| Initialize with application configuration | `candis_camera_init()` |
| Identify the active sensor and profile | `candis_camera_read_sensor_id()`, `candis_camera_get_active_profile()` |
| Enumerate sensor modes and output formats | `candis_camera_get_mode()`, `candis_camera_get_pixel_format()` |
| Start capture and obtain the actual output format | `candis_camera_stream_start()` |
| Copy a frame into application memory | `candis_camera_capture_frame()` |
| Borrow and return a DMA frame | `candis_camera_frame_acquire()`, `candis_camera_frame_release()` |
| Query, read or set an official V4L2 control | `candis_camera_control_query()`, `candis_camera_control_get()`, `candis_camera_control_set()` |
| Stop capture and release resources | `candis_camera_stop()` |

Use the returned format for stride, dimensions, and pixel encoding. Query controls after initialization and apply settings between borrowed frames during capture. The profile records which controls support readback. A write-only control returns `ESP_ERR_NOT_SUPPORTED` from `candis_camera_control_get()`. OV5640 supports setting horizontal mirror, vertical flip, and test pattern. SC2336 also supports gain, exposure value, and write-only absolute exposure in 100-microsecond units. Applications using native V4L2 can obtain the device path with `candis_camera_device()`.

## Camera example

Each `sdkconfig.<model>` fragment in the [camera example](https://github.com/jlclab-io/candis-bsp/tree/main/examples/camera) selects one driver and reference mode for [Dock v1.2 U7](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/expansion.md#dock-20-pin-headers). Match the module pinout, I/O levels, XCLK, supply rails and RESET/PWDN requirements before connecting it. The default Dock configuration leaves supply management to the application or module. Release UART0 on the expansion I²C pins before using SCCB. U7 has no dedicated RESET or PWDN line.

RGB565, YUV422, and grayscale modes can preview on the 480 × 480 AMOLED. RAW and JPEG modes capture a payload and log its size. Use a separate build directory and SDKCONFIG file for each variant.

| Fragment | Use |
| --- | --- |
| `sdkconfig.ov5640` | Select OV5640 RGB565 preview on Dock v1.2 U7 |
| `sdkconfig.still` | Display a single captured frame |

Other `sdkconfig.<model>` fragments select the corresponding sensor driver and mode. Assembly-specific supplies and control pins belong in the application resource callbacks.

[Apache-2.0](LICENSE)
