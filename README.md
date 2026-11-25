<div align="center">
  <h1>Candis BSP</h1>
  <p><strong>ESP-IDF board support for Candis</strong></p>
  <p>ESP32-S31 &nbsp;·&nbsp; 2.14-inch 480 × 480 AMOLED touchscreen &nbsp;·&nbsp; Candis Dock expansion</p>
  <p>
    <a href="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml"><img alt="ESP-IDF build" src="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml/badge.svg?branch=main"></a>
    <a href="components/candis/idf_component.yml"><img alt="Target ESP32-S31" src="https://img.shields.io/badge/Target-ESP32--S31-00979D"></a>
    <a href="https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb"><img alt="ESP-IDF 6.2.x" src="https://img.shields.io/badge/ESP--IDF-6.2.x-E7352C"></a>
    <a href="LICENSE"><img alt="License Apache-2.0" src="https://img.shields.io/badge/License-Apache--2.0-blue"></a>
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

**Prepare ESP-IDF.** Use [ESP-IDF 6.2](https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb) as the primary environment. ESP-IDF 6.1 is supported for compatibility. In the ESP-IDF checkout, run `./install.sh esp32s31` and `source ./export.sh`.

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

For a sibling local checkout, use the manifest-relative `path: ../../candis-bsp/components/candis`. Include `bsp/esp-bsp.h`, start LVGL with `bsp_display_start()`, and turn on brightness with `bsp_display_backlight_on()`. See the [API guide](docs/en/api.md#display-and-touch).

</details>

## Boards

| Board | Revision | Interfaces |
| --- | --- | --- |
| **Candis** | 1.2 | Core board with AMOLED, touch, RTC, and USB-C |
| **Candis Dock** | 1.1, 1.2 | TF (microSD), speaker connector, and two expansion headers |

Power off before fitting a baseboard. Connect one baseboard at a time across both board-to-board connectors. [Hardware and power](docs/en/hardware.md) · [Expansion and EEPROM](docs/en/expansion.md)

## Examples

- [**Minimal**](examples/candis_minimal) exercises I²C ownership and release recovery without LVGL.
- [**Display**](examples/display) brings up the AMOLED and LVGL touch UI.
- [**Audio recorder**](examples/audio/README.md) and [**music player**](examples/music_player/README.md) demo stereo recording and playback.
- [**Camera**](examples/camera) selects a DVP sensor profile for capture or AMOLED preview.
- [**Wi-Fi and BLE coexistence**](examples/bleprph_wifi_coex/README.md) combines access-point scanning, HTTP access, and a BLE GATT server.
- [**Dock**](examples/dock/README.md) and [**expansion**](examples/expansion) exercise microSD and probing of the reserved expansion EEPROM addresses.

[Browse all examples →](docs/en/examples.md)

## Components

- [`candis`](components/candis/README.md) provides the core board's display, touch, RTC, power, and peripheral APIs.
- [`tg28-esp`](components/tg28-esp/README.md) is the standalone TG28-ESP power management driver.
- [`candis-camera`](components/candis-camera/README.md) provides configurable 8-bit DVP capture and controls for 17 Espressif sensor profiles.
- [`candis-dock`](components/candis-dock/README.md) manages Dock identification, TF cards, and speaker routing.

Hardware support and configuration boundaries are listed in [component support](docs/en/component-coverage.md).

## License

[Apache-2.0](LICENSE). The music example's bundled score is [CC0-1.0](examples/music_player/README.md).
