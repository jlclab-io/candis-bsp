# Component support

[Documentation](README.md) · [简体中文](../zh/component-coverage.md)

| Component | Available features | Hardware requirements and coverage |
| --- | --- | --- |
| `candis` | AMOLED, touch, RTC, rail status, TF mounting, optional I²S audio and BMI270 | Candis rev. 1.2. Audio, TF, and motion require the corresponding populated hardware and powered rails. Review the [hardware guide](hardware.md). |
| `tg28-esp` | Telemetry, regulators, charger configuration, IRQs, and power sequencing | Configurable I²C driver. The [driver guide](tg28-esp-coverage.md) describes chip and board-level requirements. |
| `candis_dock` | Baseboard identity, TF, card detect, and PCM channel selection | Dock rev. 1.1 or 1.2 with a supported descriptor. U6 must leave GPIO39 available for card detection. Module probing reports address responses. |
| `candis_gamepad` | Joystick ADC, optional millivolt conversion, debounced buttons, quadrature decoding | Gamepad rev. 1.0. Calibrate the joystick on the assembled board. SW5 requires the shared BMI270 interrupt to remain high impedance. |
| `candis-camera` | OV5640 DVP capture, stills, streaming, and sensor controls | Use a verified OV5640 wiring and power profile. The AMOLED example defaults to RGB565. Validate alternate formats and sensor settings on your assembly. |

The examples and component packages have build coverage in CI. Qualify the application on its intended hardware, especially shared rails and GPIO, charging, RTC backup and sleep behavior, and display timing. The [expansion guide](expansion.md#bsp-support) describes which parts of the EEPROM specification are available in the BSP.
