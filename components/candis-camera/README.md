# Candis Camera

Configurable 8-bit DVP capture for ESP32-S31 and ESP-IDF 6.2. The component uses Espressif ESP Video and ESP Cam Sensor with the OV5640. Its pin, clock, and power configuration can be supplied by an application independently of the Candis board BSP.

## Preview and stills

Build the [camera example](https://github.com/jlclab-io/candis-bsp/tree/main/examples/camera) from the repository root.

```sh
idf.py --preview -C examples/camera build
```

The default build uses the Candis camera fixture and shows an 800 × 600 RGB565 feed cropped to the 480 × 480 AMOLED. Connect the OV5640 and panel and supply ALDO1. The fixture camera profile assigns GPIO39/40 to reset and power-down, GPIO46–57 to DVP and XCLK, and BLDO1, ALDO4, and ALDO3 to camera supplies. The fixture power-down pin uses open drain with a pull-up to sensor DOVDD. On other boards these pins and rails may have different uses.

| Build profile | Use |
| --- | --- |
| `sdkconfig.still` | Display a single captured frame |
| `sdkconfig.jpeg` | Save a still to a FAT-formatted TF card with the fixture's GPIO35 card-detect wiring and powered DC1SW |
| `sdkconfig.controls` | Adjust sensor settings from the serial console during preview |
| `sdkconfig.manual` | Apply manual settings before preview or capture |
| `sdkconfig.uyvy` / `sdkconfig.yuyv` | Select UYVY with software conversion or YUYV with DVP hardware conversion. Verify format and color on the actual assembly |

To use a profile, add it after `sdkconfig.defaults` in `SDKCONFIG_DEFAULTS`. Use a new build directory and `SDKCONFIG` file for each variant. The JPEG profile, for example, can be built with:

```sh
idf.py --preview -C examples/camera -B "$PWD/build/camera-jpeg" \
  -D "SDKCONFIG=$PWD/build/camera-jpeg/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.jpeg' build
```

## Capture API

Add a dependency on `components/candis-camera`. Enable DVP video, OV5640 DVP auto-detection, and PSRAM. Use [`bsp/candis_camera.h`](include/bsp/candis_camera.h) from one control task.

| Operation | API |
| --- | --- |
| Configure the camera | `candis_camera_init()` |
| List build-enabled sensor modes and active output formats | `candis_camera_get_mode()`, `candis_camera_get_pixel_format()` |
| Start a stream and obtain its actual format | `candis_camera_stream_start()` |
| Copy one frame to application-owned memory | `candis_camera_capture_frame()` |
| Borrow and return a DMA frame | `candis_camera_frame_acquire()`, `candis_camera_frame_release()` |
| Stop capture and release resources | `candis_camera_stream_stop()`, then `candis_camera_stop()` |

Choose the active OV5640 mode in Kconfig. The stream API validates a requested mode against the active mode. Use the returned format for stride, dimensions, and pixel encoding. Return a borrowed buffer before stopping or waiting on unrelated I/O.

The [OV5640 controls](include/bsp/candis_ov5640.h) cover exposure, gain, white balance, brightness, contrast, saturation, image effects, sharpness, denoise, and gamma. Apply changes between frame acquisitions. Advanced applications can use the native V4L2 device from `candis_camera_device()`. Use a single capture interface per session.

## Custom wiring

Provide data, PCLK, VSYNC, HSYNC, and SCCB settings in `candis_camera_config_t`. Use a dedicated HP I²C port or supply a shared bus with both SCCB pin fields set to `GPIO_NUM_NC`. Keep that bus alive until camera stop succeeds. Mark unused RESET, PWDN, and XCLK as `GPIO_NUM_NC`. With an external XCLK, start it before sensor detection.

Use paired resource callbacks for board-specific power. Keep their context, pins, and any clock resources valid until `candis_camera_stop()` succeeds. The release callback must be able to clean up partial acquisition. The [fixture profile](https://github.com/jlclab-io/candis-bsp/blob/main/examples/camera/main/camera_fixture.c) is a wiring example.

[Apache-2.0](LICENSE)
