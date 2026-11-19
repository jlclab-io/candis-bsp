# Candis Camera

Configurable 8-bit DVP capture for ESP32-S31 using Espressif's
`esp_video` and `esp_cam_sensor`. OV5640 is the supported sensor. ESP Video
probes the DVP sensors enabled in ESP Cam Sensor at build time. Other sensors
require separate wiring, format and hardware validation.

## Use the component

Build the repository example with ESP-IDF 6.2 for ESP32-S31.

```sh
idf.py --preview -C examples/camera build
```

The example shows a live, center-cropped preview on the 480×480 AMOLED.
Connect the panel and ensure ALDO1 is already powered. The optional
`sdkconfig.bench` overlay enables panel power management and 80 MHz QSPI
on a characterized fixture. The serial log reports capture and panel FPS
and elapsed time from capture dequeue to the final panel DMA completion.

## Install in another project

Add a local path or a pinned Git dependency in the app component manifest
(`main/idf_component.yml`). The camera component resolves independently
of the Candis board BSP.

```yaml
dependencies:
  candis-camera:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis-camera
    version: CANDIS_COMMIT
```

For a local checkout, use the path to `components/candis-camera` relative
to the manifest. Once the component is published, use
`idf.py add-dependency "NAMESPACE/candis-camera==PUBLISHED_VERSION"`.

Enable DVP video, OV5640 DVP auto-detection and PSRAM. The example's
`sdkconfig.defaults` selects the 800×600 RGB565 mode. Include
`bsp/candis_camera.h` and call `candis_camera_init()` with your wiring
and resource configuration from a single control task. The repository
preview example provides a fixture configuration in `main/camera_fixture.c`
and uses `candis` for panel output and shared power management.

## Capture API

| Operation | API |
| --- | --- |
| Enumerate sensor modes enabled in ESP Cam Sensor | `candis_camera_get_mode()` |
| Verify the sensor mode, set output FourCC and timeout, then start capture | `candis_camera_stream_start()` |
| Copy one still frame into an application buffer | `candis_camera_capture_frame()` |
| Borrow and return a frame for preview or streaming | `candis_camera_frame_acquire()` / `candis_camera_frame_release()` |
| Stop capture and release hardware | `candis_camera_stream_stop()` / `candis_camera_stop()` |

The stream API uses ESP Video's V4L2 capture and reuses two DMA buffers.
Use the returned format to interpret stride, dimensions and pixel encoding.
For a still image, copy one frame into a buffer sized for the selected
format, then save or encode it in your application. The tested example
captures RGB565. Available formats depend on the enabled sensor modes.
Select the active OV5640 mode in Kconfig. With `esp_cam_sensor` 2.6.0,
an explicit sensor mode change can crash the upstream OV5640 driver.
`candis_camera_stream_start()` checks any requested mode against the
active one and returns `ESP_ERR_NOT_SUPPORTED` on a mismatch.

For continuous capture, consume or copy the borrowed frame before returning
it, and return it before waiting for a network client. The fixture preview
uses this API and returns each frame before starting AMOLED DMA. Advanced
applications can use the native V4L2 node from `candis_camera_device()` for
direct V4L2 controls. Use one capture interface per camera session.

## Custom wiring

Fill `candis_camera_config_t` and call `candis_camera_init()`.
Supply eight DVP data pins, PCLK, VSYNC, HSYNC and the SCCB frequency.
Choose a dedicated HP I²C port and pins, or pass an existing bus with both
SCCB pin fields set to `GPIO_NUM_NC`. With a shared bus, keep it alive until
stop succeeds. Set unused RESET, PWDN and XCLK pins to `GPIO_NUM_NC`.
An externally supplied XCLK must be running before sensor detection.

Use an acquire/release callback pair for board-specific power and bus
ownership. The acquire callback runs while RESET and PWDN are asserted.
Release runs after the DVP device stops and the sensor is held in reset.
Release must handle a partial acquisition and retry its unfinished cleanup
if it returns an error. Reserve the configured pins, LEDC and ESP Video
DVP instance for the lifetime of the camera. Preserve the resource
callback context until `candis_camera_stop()` succeeds.

## Hardware and validation

- The validation fixture uses GPIO39 for RESET, GPIO40 for PWDN and GPIO46–57
  for DVP and XCLK. It uses BLDO1, ALDO4 and ALDO3 for the sensor supplies.
  PWDN uses open-drain control with the fixture's pull-up to 2.8 V DOVDD.
  Reserve those pins, rails, LEDC timer 1 and LEDC channel 1 for the
  validation fixture while capturing.
  The formal expansion boards can use GPIO39 for TF card detection and
  some DVP pins for game inputs.
- On the validation fixture, the public API completed two start/stop
  cycles at 800×600 RGB565. Each cycle copied a 960,000-byte still frame
  and borrowed and returned 32 frames at 14.38 FPS. The 80 MHz QSPI
  preview reached 14.19–14.29 FPS across eight sample windows with no
  invalid frames, TE timeouts or display transfer errors.
