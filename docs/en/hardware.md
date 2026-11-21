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
- **Display timing.** The BSP defaults to 80 MHz QSPI. For a 50 MHz application configuration, set `bench_qspi_80mhz = false` in `candis_display_config_t`. Qualify the chosen speed over your operating conditions.
- **Audio.** Supply ALDO2 and connect the intended microphones and speakers before using the audio examples. Use the [Dock speaker wiring](../../components/candis_dock/README.md#hardware) to select the correct PCM channel.
- **Expansion.** DC1SW is a shared rail for TF, the baseboard EEPROM, BMI270, and attached Dock modules. Check the complete load and power sequence before enabling rail management. Connect or remove baseboards and modules with all supplies disconnected.
- **USB Host.** The Type-C circuit controls connector VBUS and current limiting. Host stop releases the data stack and PHY. Check the source setup and peripheral load before using Host.

## Expansion interfaces

| Resource | Assignment |
| --- | --- |
| Expansion I²C | I²C1, SCL GPIO58, SDA GPIO59. Release UART0 and any external drivers on both pins before probing |
| Baseboard EEPROM | `0x50` on expansion I²C with DC1SW powered. Current BSP support is described in [Expansion hardware](expansion.md#bsp-support) |
| TF card | Four-bit SDMMC, D0–D3 GPIO20–23, CLK GPIO24, CMD GPIO25. Card detect on Dock is GPIO39 |
| Motion | BMI270 at `0x68` on main I²C, supplied by DC1SW. INT1 connects to GPIO38. The BSP polls the sensor |
| M1 motor | GPIO5 drives an active-high low-side switch. BLDO2 supplies the motor at 2700 mV during a BSP pulse. Confirm the fitted load and USB input |
| OV5640 camera | GPIO39/40 for control and GPIO46–57 for DVP and XCLK. Use the [camera wiring profile](../../components/candis-camera/README.md#custom-wiring) for your hardware |

See [Expansion hardware](expansion.md) for the BTB and Dock connector pinouts.
