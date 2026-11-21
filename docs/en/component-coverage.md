# Component support

[Documentation](README.md) · [简体中文](../zh/component-coverage.md)

| Component | Available features | Hardware requirements and coverage |
| --- | --- | --- |
| `candis` | ESP-BSP interfaces, AMOLED, touch, RTC, power and optional LVGL, audio, BMI270 and USB Device/Host | Candis core board. TF requires an identified Dock. BMI270 and motor operation require the fitted hardware and supply. See the [hardware guide](hardware.md). |
| `tg28-esp` | Telemetry, regulators, charger configuration, IRQs, and power sequencing | Configurable I²C driver. The [driver guide](tg28-esp-coverage.md) describes chip and board-level requirements. |
| `candis-dock` | Baseboard identity, TF, card detect, and PCM channel selection | Dock rev. 1.1 or 1.2 with a supported descriptor. U6 must leave GPIO39 available for card detection. Module probing reports address responses. |
| `candis-camera` | OV5640 DVP capture, stills, streaming, and sensor controls | Use a verified OV5640 wiring and power profile. The AMOLED example defaults to RGB565. Validate alternate formats and sensor settings on your assembly. |

The examples and component packages have build coverage in CI. Qualify the application on its intended hardware, especially shared rails and GPIO, charging, RTC backup and sleep behavior, and display timing. The [expansion guide](expansion.md#bsp-support) describes which parts of the EEPROM specification are available in the BSP.
