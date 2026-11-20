<div align="center">
  <h1>Candis BSP</h1>
  <p><strong>ESP-IDF board support for Candis</strong></p>
  <p>ESP32-S31 &nbsp;·&nbsp; 2.14-inch 480 × 480 AMOLED touchscreen &nbsp;·&nbsp; Dock and Gamepad expansion</p>
  <p>
    <a href="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml"><img alt="ESP-IDF build" src="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml/badge.svg?branch=main"></a>
  </p>
  <p>
    <a href="#quick-start"><strong>Quick start</strong></a> ·
    <a href="#boards">Boards</a> ·
    <a href="docs/en/examples.md">Examples</a> ·
    <a href="docs/en/README.md">Documentation</a> ·
    <a href="README_ZH.md">简体中文</a>
  </p>
</div>

## Quick start

**Prepare ESP-IDF.** Use [ESP-IDF 6.2](https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb). In the ESP-IDF checkout, run `./install.sh esp32s31` and `source ./export.sh`.

**Build the display example.** ESP32-S31 uses `--preview` with the linked SDK revision.

```sh
git clone https://github.com/jlclab-io/candis-bsp.git
cd candis-bsp
idf.py --preview -C examples/display build
```

**Flash and try touch.** Connect USB-C with a data cable, confirm the AMOLED is powered, and follow the [flashing guide](docs/en/usb.md). Open **Partial**, **Full**, or **Scan** and drag a finger across the display.

> [!TIP]
> If flashing fails, hold **PWRON** for **6 seconds** to power off. Then hold **BOOT** and press **PWRON** for **2 seconds** to power on in ROM download mode. Select the ROM port and retry flashing.

<details>
<summary><strong>Add Candis to your application</strong></summary>

Add this dependency to your application component's `idf_component.yml`, such as `main/idf_component.yml`. Replace `<commit-or-tag>` with a BSP revision.

```yaml
dependencies:
  candis:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis
    version: "<commit-or-tag>"
```

For a sibling local checkout, use the manifest-relative `path: ../../candis-bsp/components/candis`. Include `bsp/esp-bsp.h` and call `bsp_display_start()` to run LVGL. The [API guide](docs/en/api.md#display-and-touch) has a complete example.

</details>

## Boards

| Board | Revision | Interfaces |
| --- | --- | --- |
| **Candis** | 1.2 | Core board with AMOLED, touch, RTC, and USB-C |
| **Candis Dock** | 1.1, 1.2 | TF (microSD), speaker connector, and two expansion headers |
| **Candis Gamepad** | 1.0 | Joystick, buttons, and rotary encoder |

Power off before fitting a baseboard. Connect one baseboard at a time across both board-to-board connectors. [Hardware and power](docs/en/hardware.md) · [Expansion and EEPROM](docs/en/expansion.md)

## Examples

- [**Display**](examples/display) brings up the AMOLED and LVGL touch UI.
- [**Audio recorder**](examples/audio/README.md) and [**music player**](examples/music_player/README.md) demo stereo recording and playback.
- [**Camera**](examples/camera) previews and captures frames from an OV5640.
- [**Dock**](examples/dock/README.md) and [**expansion**](examples/expansion) exercise microSD and Gamepad input.

[Browse all examples →](docs/en/examples.md)

## Components

- [`candis`](components/candis/README.md) provides the core board's display, touch, RTC, power, and peripheral APIs.
- [`tg28-esp`](components/tg28-esp/README.md) is the standalone TG28-ESP power management driver.
- [`candis-camera`](components/candis-camera/README.md) provides configurable OV5640 DVP capture.
- [`candis_dock`](components/candis_dock/README.md) manages Dock identification, TF cards, and speaker routing.
- [`candis_gamepad`](components/candis_gamepad/README.md) reads joystick axes, buttons, and the encoder.

The BSP is in development preview. See [component support](docs/en/component-coverage.md) for hardware coverage.

## License

[Apache-2.0](LICENSE). The music example's bundled score is [CC0-1.0](examples/music_player/README.md).
