# Candis BSP

**English** · [简体中文](README_ZH.md)

[![ESP-IDF build](https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml)

[Quick start](#quick-start) · [Components](#components) · [Examples](#examples) · [Documentation](docs/en/README.md)

Board support for **Candis**, an ESP32-S31 development board with a 2.14-inch, 480 × 480 AMOLED touchscreen. The BSP initializes the display, touch controller, RTC, and board power interfaces, with APIs for audio, motion sensing, and storage on compatible hardware. The project is in development preview.

## Boards

| Hardware | Revision | Support |
| --- | --- | --- |
| Candis | 1.2 | Standalone core board with AMOLED, capacitive touch, RTC, and native USB-C |
| Candis Dock | 1.1, 1.2 | Optional baseboard with TF (microSD), a speaker connector, and two expansion headers |
| Candis Gamepad | 1.0 | Optional baseboard with joystick axes, buttons, and a rotary encoder |

Fit one baseboard at a time using both board-to-board connectors, with power disconnected. Check the [hardware guide](docs/en/hardware.md) for rail and pin requirements.

## Quick start

1. Use ESP-IDF 6.2 and the ESP32-S31 toolchain. From your ESP-IDF checkout, run `./install.sh esp32s31` and `source ./export.sh`. The [CI-tested ESP-IDF revision](https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb) requires `--preview` for ESP32-S31.
2. Build the display example.

   ```sh
   git clone https://github.com/jlclab-io/candis-bsp.git
   cd candis-bsp
   idf.py --preview -C examples/display build
   ```

3. Connect the board with a data-capable USB cable. Confirm the AMOLED supply is on, then follow [USB and flashing](docs/en/usb.md) to install the example. Use **Partial**, **Full**, or **Scan** on the display and drag a finger to try touch input.

### Add Candis to an application

Add a dependency to your application component's `idf_component.yml`, such as `main/idf_component.yml`. Replace `<commit-or-tag>` with a Candis BSP Git revision.

```yaml
dependencies:
  candis:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis
    version: "<commit-or-tag>"
```

For local development, use a `path` relative to the manifest, such as `../../candis-bsp/components/candis` for sibling checkouts. Include `bsp/esp-bsp.h` and start LVGL with `bsp_display_start()`. See the [API guide](docs/en/api.md#display-and-touch) for configuration and an LVGL example.

## Components

| Component | Use |
| --- | --- |
| [`candis`](components/candis/README.md) | Core board support and shared peripheral resources |
| [`tg28-esp`](components/tg28-esp/README.md) | Standalone TG28-ESP power-management driver |
| [`candis-camera`](components/candis-camera/README.md) | Configurable DVP capture for OV5640 |
| [`candis_dock`](components/candis_dock/README.md) | Dock identification, TF card support, and speaker routing |
| [`candis_gamepad`](components/candis_gamepad/README.md) | Gamepad axes, buttons, and encoder |

## Examples

| Start with | What it demonstrates |
| --- | --- |
| [Display](examples/display) | LVGL on AMOLED with touch input |
| [Audio recorder](examples/audio/README.md) · [Music player](examples/music_player/README.md) | Stereo recording and playback |
| [Camera](examples/camera) | OV5640 preview and still capture |
| [Dock](examples/dock/README.md) · [Expansion](examples/expansion) | Dock storage and Gamepad input |

See the [full example catalog](docs/en/examples.md), [hardware guide](docs/en/hardware.md), and [EEPROM V1 specification](docs/en/eeprom-v1.md) for board integration.

## License

[Apache-2.0](LICENSE). The music example's bundled score is [CC0-1.0](examples/music_player/README.md).
