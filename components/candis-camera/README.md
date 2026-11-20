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

The example shows a live, center-cropped preview on the 480×480 AMOLED
using the BSP's default 80 MHz QSPI setting. Connect the panel and ensure
ALDO1 is powered. The optional `sdkconfig.bench` overlay enables panel power
management on a characterized fixture. The serial log reports capture and panel
FPS and elapsed time from capture dequeue to the final panel DMA completion.

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
| Enumerate DVP output FourCCs for the active sensor mode | `candis_camera_get_pixel_format()` |
| Verify the sensor mode, set output FourCC and timeout, then start capture | `candis_camera_stream_start()` |
| Copy one still frame into an application buffer | `candis_camera_capture_frame()` |
| Borrow and return a frame for preview or streaming | `candis_camera_frame_acquire()` / `candis_camera_frame_release()` |
| Identify the sensor, flip its orientation or show its test pattern | `candis_camera_read_sensor_id()`, `candis_camera_set_flip()`, `candis_camera_set_test_pattern()` |
| Read and configure OV5640 exposure, analog gain and ISP white balance | `candis_ov5640_get_controls()`, `candis_ov5640_set_controls()` |
| Read and configure brightness, contrast and saturation | `candis_ov5640_get_isp()`, `candis_ov5640_set_isp()` |
| Read and configure the auto/manual sharpness and denoise modes | `candis_ov5640_get_cip()`, `candis_ov5640_set_cip()` |
| Stop capture and release hardware | `candis_camera_stream_stop()` / `candis_camera_stop()` |

The stream API uses ESP Video's V4L2 capture and reuses two DMA buffers.
Use the returned format to interpret stride, dimensions and pixel encoding.
For a still image, copy one frame into a buffer sized for the selected
format, then save or encode it in your application. The tested example
captures RGB565. Available formats depend on the enabled sensor modes.
The example's `sdkconfig.still` overlay captures one still image, displays
it for five seconds and releases camera and display resources. The optional
`CANDIS_CAMERA_TEST_PATTERN` switch exercises the sensor's diagnostic output.
Call sensor controls between frame acquisitions on the capture task.

Include `bsp/candis_ov5640.h` for independent auto/manual exposure, analog gain
and RGB white-balance settings. The OV5640 applies a complete group at a frame
boundary. Manual exposure uses whole lines below the active mode's frame length.
Manual analog gain uses six integer bits and four fractional bits. White-balance
gains use units of 1/1024. The APIs require the OV5640 ISP white-balance block to
be enabled. After a transport error, allow control cleanup and read back the
sensor state before retrying.

The ISP API adjusts brightness, contrast and manual or automatic saturation
through the sensor's SDE block. Fixed Y/U/V effects share the same registers,
so the API reports them as unsupported. Hue, gamma, color bars and lens
correction remain accessible through the native V4L2 node.
Automatic exposure and white-balance values can change during multi-register
reads.

The gamma API exposes the 16 sensor registers at 0x5481-0x5490. The first
15 values must be monotonically nondecreasing. The final register is retained
as the sensor slope code. The console loads the two Espressif reference curves
with GAMMA 0 and GAMMA 1.

The CIP API selects automatic or manual sharpness levels from -3 to +3 and
denoise levels from 0 to 8. It preserves the sensor's automatic fields and
resolves levels using the Espressif OV5640 reference mapping. Readback reports
the mapped level only when the manual register pattern matches that mapping.

Built-in image effects include normal, negative, grayscale, red/green/blue
tint and sepia. CMX saturation selects an Espressif reference register set
from -4 to +4. Fixed SDE output effects and CMX saturation are mutually exclusive.

With automatic exposure enabled, `candis_ov5640_set_ae_level()` adjusts the
exposure target from -5 through +5. `candis_ov5640_set_white_balance_preset()`
selects auto, sunny, cloudy, office, or home white balance.
The optional `sdkconfig.manual` overlay applies exposure, gain, white balance
and ISP presets, then waits for frames and verifies register readback before
preview or JPEG. Optical response and settling need validation on the fixture.

Use `sdkconfig.controls` for live tuning over USB Serial/JTAG. The commands use
decimal values and end with a newline.

| Command | Effect |
| --- | --- |
| `READ` | Report exposure, active frame length, gain, white balance and ISP settings |
| `AUTO` | Enable automatic exposure, analog gain and white balance |
| `MANUAL 150 32 1024 1024 1024` | Set 150 exposure lines, 2× analog gain and unity RGB gains |
| `AELEVEL 2` | Adjust the automatic exposure target |
| `WB CLOUDY` | Select the cloudy white-balance preset |
| `ISP 16 32 64 64` | Set brightness, contrast and manual U/V saturation codes |
| `CIP MANUAL 2 5` | Set manual sharpness and denoise strengths |
| `CIP AUTO 5` | Use automatic sharpness and set manual denoise strength |
| `EFFECT SEPIA` | Apply a built-in sensor image effect |
| `SATURATION -2` | Select a color-matrix saturation level |
| `GAMMA 0` | Load and enable the default reference gamma curve |
| `STOP` | End preview and release camera and display resources |

Send `READ` after at least one frame to confirm a scheduled write. Replace the
example exposure with a value below the frame length reported by the active mode.

The `sdkconfig.uyvy` profile uses the sensor's native UYVY mode and BT.601
software conversion. This differs from `sdkconfig.yuyv`, which requests
Espressif's YUYV-to-RGB565X DVP hardware conversion. Both profiles can use the
hardware JPEG encoder in the still-save example.

On the Candis validation fixture, use `sdkconfig.jpeg` to save the full
800×600 still as a JPEG with the ESP32-S31 encoder. The example displays the
center crop, releases the camera, then mounts a FAT-formatted TF card using
the fixture's GPIO35 card-detect pin and DC1SW. It saves a new `IMG_0001.JPG`
or the next available number, then reopens the file and compares it with
the encoder output. The `Saved and verified /sdcard/IMG_...` log identifies
the saved file. This configuration uses the sensor's default controls and
skips the first ten frames to let automatic exposure settle.

```sh
idf.py --preview -C examples/camera -B build/jpeg \
  -D SDKCONFIG=build/jpeg/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bench;sdkconfig.jpeg' build
```

Select the active OV5640 mode in Kconfig. With `esp_cam_sensor` 2.6.0,
an explicit sensor mode change can crash the upstream OV5640 driver.
`candis_camera_stream_start()` checks any requested mode against the
active one and returns `ESP_ERR_NOT_SUPPORTED` on a mismatch.

The optional `sdkconfig.yuyv` selects 800×600 YUYV. The preview checks that
ESP Video advertises RGB565X and requests the DVP hardware conversion.
The output is treated as big-endian RGB565 per the converter contract and
normalized for both AMOLED and JPEG. Combine with `sdkconfig.jpeg` for the
one-shot capture. Test colors, byte order and actual frame timing on the fixture.

The `sdkconfig.uyvy` profile receives native UYVY. The example converts packed
UYVY or YUYV to RGB565 in software using BT.601. It follows explicit full or
limited-range metadata and uses full range when ESP Video provides its default.
It converts the cropped output for preview and the full frame for JPEG.

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
