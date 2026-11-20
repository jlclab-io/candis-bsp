# Candis hardware

[Documentation](README.md) · [简体中文](../zh/hardware.md)

The `candis` component targets Candis revision 1.2. The board uses an ESP32-S31 and a 2.14-inch AM214Q480480LK2 AMOLED module. Pin definitions are in [`candis_board.h`](../../components/candis/candis_board.h).

## Onboard interfaces

| Interface | Connection |
| --- | --- |
| Main I²C | I²C0, SCL GPIO6, SDA GPIO7, external pull-ups |
| Display | ST7801N, 480 × 480 RGB565, SPI2 QSPI with CLK GPIO12, CS GPIO10, and D0–D3 GPIO11/13/14/9. RESET GPIO2, TE GPIO8 |
| Touch | CST9220 on main I²C at `0x5A`, RESET GPIO3, INT GPIO1 |
| Power management | TG28-ESP on main I²C at `0x34` |
| RTC | RX8130CE on main I²C at `0x32`, GPIO0 shared with the PMU interrupt |
| Audio | ES8389 on main I²C at `0x10`. I²S MCLK/BCLK/WS/DOUT/DIN on GPIO15–19, amplifier enable GPIO4 |
| USB-C | Native USB OTG for ROM download and the example CDC console |

## Power

- **Display.** Use a verified supply on ALDO1 before starting the panel. The BSP leaves the rail unchanged by default. The panel's VDDIO limit is 3.35 V. A nominal 3.3 V ALDO1 with ±3% tolerance can reach 3.399 V before transients. Check the actual rail against the module's limits before permitting software to enable it.
- **Display timing.** The BSP defaults to 80 MHz QSPI. For a 50 MHz application configuration, set `bench_qspi_80mhz = false` in `candis_display_config_t`. Qualify the chosen speed over your operating conditions.
- **Audio.** Supply ALDO2 and connect the intended microphones and speakers before using the audio examples. Use the [Dock speaker wiring](../../components/candis_dock/README.md#hardware) to select the correct PCM channel.
- **Expansion.** DC1SW is a shared rail for TF, the baseboard EEPROM, BMI270, and attached Dock modules. Check the complete load and power sequence before enabling rail management. Connect or remove baseboards and modules with all supplies disconnected.

## Expansion interfaces

| Resource | Assignment |
| --- | --- |
| Expansion I²C | I²C1, SCL GPIO58, SDA GPIO59. Release UART0 and any external drivers on both pins before probing |
| Baseboard EEPROM | `0x50` on expansion I²C with DC1SW powered. Current BSP support is described in [Expansion hardware](expansion.md#bsp-support) |
| TF card | Four-bit SDMMC, D0–D3 GPIO20–23, CLK GPIO24, CMD GPIO25. Card detect is GPIO39 on Dock and Gamepad, GPIO35 on the evaluation fixture |
| Motion | BMI270 at `0x68` on main I²C, powered from the expansion domain |
| Gamepad SW5 | Shares GPIO38 with BMI270 INT1. Enable SW5 only with INT1 kept high impedance and the required rails stable |
| OV5640 fixture | GPIO39/40 for control and GPIO46–57 for DVP and XCLK. Use the [camera wiring profile](../../components/candis-camera/README.md#custom-wiring) for your hardware |

The evaluation fixture has its own pin and rail assignments. Use the profile for the connected hardware when building camera, storage, or input examples. See [Expansion hardware](expansion.md) for the BTB and Dock connector pinouts.
