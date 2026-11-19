# Candis OV5640

Optional OV5640 DVP board support for Candis hardware revision 1.2 with the
validation fixture. It uses Espressif's `esp_video` and `esp_cam_sensor` and
shares the Candis PMU and main I²C bus.

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

For local development, add `candis_ov5640` to the application's
`idf_component.yml` with a `path` to `components/candis_ov5640`. The
component resolves `candis` from the same checkout. Registry installation
depends on a published Candis BSP version.

Enable the DVP video device, OV5640 DVP detection and PSRAM.
See the example's `sdkconfig.defaults` for the 800×600 RGB565 configuration.
Include `bsp/candis_ov5640.h` and call `candis_ov5640_start()` from a single
control task. Open the V4L2 node returned by `candis_ov5640_device()` to capture.
Stop streaming, unmap the frame buffers and close the node before calling
`candis_ov5640_stop()`.

## Hardware and validation

- The validation fixture uses GPIO39 for RESET, GPIO40 for PWDN and GPIO46–57
  for DVP and XCLK. It uses BLDO1, ALDO4 and ALDO3 for the sensor supplies.
  PWDN uses open-drain control with the fixture's pull-up to 2.8 V DOVDD.
  Reserve those pins, rails, LEDC timer 1 and LEDC channel 1 for the
  validation fixture while capturing.
  The formal expansion boards can use GPIO39 for TF card detection and
  some DVP pins for game inputs.
- The validation fixture previously completed two 32-frame capture cycles
  in 800×600 RGB565 at about 14.27 FPS, with stream shutdown and release
  after each cycle. See the repository hardware notes for the test scope.
