# Candis hardware

[Documentation](README.md) · [简体中文](../zh/hardware.md)

The `candis` component targets Candis revision 1.2. The board uses an ESP32-S31 and a 2.14-inch AM214Q480480LK2 AMOLED module. Pin definitions are in [`candis_board.h`](../../components/candis/priv_include/candis_board.h).

## Onboard interfaces

| Interface | Connection |
| --- | --- |
| Main I²C | I²C0, SCL GPIO6, SDA GPIO7, external pull-ups |
| Display | ST7801N, 480 × 480 RGB565, SPI2 QSPI with CLK GPIO12, CS GPIO10, and D0–D3 GPIO11/13/14/9. RESET GPIO2, TE GPIO8 |
| Touch | CST9220 on main I²C at `0x5A`, RESET GPIO3, INT GPIO1 |
| Power management | TG28-ESP on main I²C at `0x34` |
| RTC | RX8130CE on main I²C at `0x32`, GPIO0 shared with the PMU interrupt |
| Audio | ES8389 on main I²C at `0x10`. I²S MCLK/BCLK/WS/DOUT/DIN on GPIO15–19, amplifier enable GPIO4 |
| USB-C | Native USB OTG for ROM download, CDC Device and USB Host |

## Power

- **Display.** Use a verified supply on ALDO1 before starting the panel. The BSP leaves the rail unchanged by default. The panel's VDDIO limit is 3.35 V. A nominal 3.3 V ALDO1 with ±3% tolerance can reach 3.399 V before transients. Check the actual rail against the module's limits before permitting software to enable it.
- **Display timing.** The BSP defaults to 80 MHz QSPI. A 50 MHz configuration is also available through `candis_display_config_t`.
- **Audio.** Supply ALDO2 and connect the intended microphones and speakers before using the audio examples. Use the [Dock speaker wiring](../../components/candis-dock/README.md#hardware) to select the correct PCM channel.
- **Expansion.** The BTB exposes TG28 rails, GPIO, I²C, USB-related pins, audio signals, and power. The core BSP does not assign baseboard devices or GPIO. On the current Dock, DC1SW is shared by TF, the baseboard EEPROM, BMI270 and the U6/U7 expansion headers. Configure rail management against the complete supply-domain load and power sequence.
- **USB Host.** The Type-C circuit controls connector VBUS and current limiting. Host stop releases the data stack and PHY. Check the source setup and peripheral load before using Host.

## Expansion interfaces

The `candis` component owns the fixed core-board resources above. Baseboard device models, addresses, storage pins, motor PWM, and optional I²C controllers stay in the application or in a dedicated baseboard component.

| Assembly | Assignment owner |
| --- | --- |
| Core BSP | No implicit baseboard SDMMC slot, card detect, IMU address, motor GPIO, or expansion I²C controller |
| Candis Dock rev. 1.1/1.2 | `candis-dock` owns I²C1 on GPIO58/59, descriptor `0x50`, TF GPIO20–25 with CD GPIO39, Dock speaker routing, and the Dock revision rules |
| Application-owned peripherals | The storage, motion, motor, and camera examples show how to supply application-specific pins and devices. Adjust each profile for the actual assembly. See [TF card](../../examples/tf_card), [motion](../../examples/motion), [motor](../../examples/motor), and [camera](../../examples/camera) |
| Dock camera connector | The current Dock exposes 8-bit DVP on GPIO46–57, SCCB on GPIO58/59, and PMU-controlled BLDO1, DC1SW, ALDO3, and ALDO4. The camera example passes this wiring through its resource adapter. See the [U7 connector pin map](expansion.md#dock-20-pin-headers) |

See [Expansion hardware](expansion.md) for the BTB and Dock connector pinouts.
