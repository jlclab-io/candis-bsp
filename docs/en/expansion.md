# Candis expansion hardware

[Documentation](README.md) · [简体中文](../zh/expansion.md)

The Candis core board connects one BTB baseboard through the 30-pin CN1 and 50-pin CN2 connectors. The supported baseboards are Candis Dock rev. 1.1 and 1.2. The core `candis` component decodes the baseboard descriptor. The `candis-dock` component defines the Dock's fixed bus, EEPROM, TF, BMI270 and speaker resources.

```text
Candis core board
  CN1 + CN2 → Dock
               ├ U10 EEPROM → 0x50
               ├ LEFT  U6   → 0x51
               └ RIGHT U7   → 0x52
```

## EEPROM wiring

Dock rev. 1.2 uses the AT24C02 (`C688857`) at U10. A0/A1/A2 and WP are tied to GND. VCC is on DC1SW. U10's SDA and SCL connect to the expansion bus through Q3 (2N7002DW), with its gates and both 4.7 kΩ pull-ups tied to DC1SW. Dock U6/U7 share the expansion bus side of Q3. The core board has no baseboard descriptor EEPROM.

## BTB pinout

The core board uses `C41421082` at CN1 and `C41421084` at CN2. Dock rev. 1.2 uses the `C41421022` and `C41421024` mating connectors. The following mapping is based on schematic, PCB connectivity and connector orientation. Multiple pins in a row match in the listed order. For example, core CN2.11/12 mate with baseboard CN2.15/14. Baseboard CN2.11/12 carry VBAT. Confirm pin 1 and the reference designator on the matching connector during assembly.

### CN1

| Core board pin | Dock rev. 1.2 pin | Net |
| --- | --- | --- |
| 1, 2 | 29, 27 | `RSPK_N` |
| 3 | 25 | `GPIO42` |
| 4 | 23 | `GPIO43` |
| 5 | 21 | `GPIO44` |
| 6 | 19 | `GPIO45` |
| 7 | 17 | `GPIO40` |
| 8 | 15 | `GPIO39` |
| 9 | 13 | `GPIO38` |
| 10 | 11 | `GPIO37` |
| 11 | 9 | `GPIO36` |
| 12 | 7 | `GPIO35` |
| 13, 18 | 5, 6 | `GND` |
| 14, 15 | 3, 1 | `LSPK_N` |
| 16, 17 | 2, 4 | `LSPK_P` |
| 19 | 8 | `GPIO5` |
| 20 | 10 | `ESP32S31_CHIP_PU` |
| 21 | 12 | `GPIO20` |
| 22 | 14 | `GPIO21` |
| 23 | 16 | `GPIO22` |
| 24 | 18 | `GPIO23` |
| 25 | 20 | `GPIO24` |
| 26 | 22 | `GPIO25` |
| 27 | 24 | `GPIO33` |
| 28 | 26 | `GPIO34` |
| 29, 30 | 28, 30 | `RSPK_P` |

### CN2

| Core board pin | Dock rev. 1.2 pin | Net |
| --- | --- | --- |
| 1 | 25 | `GPIO48` |
| 2 | 24 | `GPIO49` |
| 3 | 23 | `GPIO50` |
| 4 | 22 | `GPIO51` |
| 5 | 21 | `GPIO52` |
| 6 | 20 | `GPIO53` |
| 7 | 19 | `GPIO54` |
| 8 | 18 | `GPIO55` |
| 9 | 17 | `GPIO56` |
| 10 | 16 | `GPIO57` |
| 11 | 15 | `GPIO58` → `EXP_SCL` |
| 12 | 14 | `GPIO59` → `EXP_SDA` |
| 13 | 13 | `TG28_NTC` |
| 14, 15, 16, 17 | 12, 11, 10, 9 | `TG28_VBAT+` |
| 18, 19 | 8, 7 | `TG28_DC1SW` |
| 20, 21 | 6, 5 | `TG28_ALDO3` |
| 22, 23 | 4, 3 | `TG28_ALDO4` |
| 24, 25 | 2, 1 | `TG28_BLDO2` |
| 26 | 26 | `ESP32S31_I2C_SCL` |
| 27 | 27 | `ESP32S31_I2C_SDA` |
| 28, 29 | 28, 29 | `TG28_BLDO1` |
| 30, 31, 32, 33, 37, 38, 39, 40, 44 | 30, 31, 32, 33, 37, 38, 39, 40, 44 | `GND` |
| 34, 35, 36 | 34, 35, 36 | `TG28_VSYS` |
| 41 | 41 | `TG28_PWRON` |
| 42, 43 | 42, 43 | `TG28_VRTC` |
| 45 | 45 | `GPIO47` |
| 46 | 46 | `GPIO46` |
| 47, 48, 49, 50 | 47, 48, 49, 50 | `BB_5V_IN` |

### Dock 20-pin headers

On Dock rev. 1.2, LEFT is U6 and RIGHT is U7. Both use a `C46635855` dual-row header.

| Pin | LEFT (U6) | RIGHT (U7) |
| ---: | --- | --- |
| 1 | `GND` | `GPIO49` |
| 2 | `GND` | `GPIO48` |
| 3 | `GPIO33` | `GPIO50` |
| 4 | `GPIO35` | `GPIO46` |
| 5 | `GPIO34` | `GPIO51` |
| 6 | `GPIO36` | `GPIO47` |
| 7 | `TG28_ALDO4` | `GPIO52` |
| 8 | `GPIO37` | `TG28_BLDO1` |
| 9 | `TG28_ALDO3` | `GPIO53` |
| 10 | `GPIO39` | `TG28_DC1SW` |
| 11 | `TG28_DC1SW` | `GPIO54` |
| 12 | `GPIO40` | `TG28_ALDO3` |
| 13 | `TG28_BLDO1` | `GPIO55` |
| 14 | `GPIO45` | `TG28_ALDO4` |
| 15 | `EXP_SDA` | `GPIO56` |
| 16 | `GPIO44` | `EXP_SCL` |
| 17 | `EXP_SCL` | `GPIO57` |
| 18 | `GPIO43` | `EXP_SDA` |
| 19 | `GND` | `GPIO35` |
| 20 | `GPIO42` | `GND` |

The two headers share GPIO35, `EXP_SCL`, `EXP_SDA`, DC1SW, ALDO3, ALDO4, BLDO1 and ground. U6.10 also connects GPIO39 to Dock card detect and its pull-up. Both headers use the same supply domain and address space. Q3 is on the U10 branch and U6/U7 remain directly connected.

U6 and U7 are configurable expansion headers. The BSP assigns neither address to a module model, functional device or driver. Applications that use an expansion header define the module identity, functional addresses, GPIO, supply and resource exclusions in their own board configuration.

Dock rev. 1.2 U7 can be used with the camera example's 8-bit DVP configuration. GPIO46–53 carry D0–D7. GPIO54, GPIO55, GPIO56 and GPIO57 carry PCLK, XCLK, VSYNC and HSYNC. SCCB shares `EXP_SCL` and `EXP_SDA`. U7 has no dedicated RESET or PWDN signal.

## Locations and addresses

| Location | 7-bit address | A2/A1/A0 | Basis |
| --- | --- | --- | --- |
| BTB U10 | `0x50` | `000` | Confirmed in the Dock rev. 1.2 design |
| Dock LEFT (U6) | `0x51` | `001` | Required by this specification. Strap on the module or adapter |
| Dock RIGHT (U7) | `0x52` | `010` | Required by this specification. Strap on the module or adapter |

`0x50` is the fixed address of Dock U10. `0x51` and `0x52` are module EEPROM addresses reserved by the Candis expansion specification for U6 and U7. A module or adapter must set A2/A1/A0 for its header with power off. The address identifies an EEPROM communication location. It does not define a module product, functional device or driver.

## Discovery interfaces

- After DC1SW is powered and UART0 has been released from GPIO58/59, call `candis_dock_probe()` to read and validate the Dock descriptor at `0x50`. Use `candis_dock_get_state()` to distinguish absent, recognized and initialized states.
- When the application supplies its own I²C bus and address, call `candis_baseboard_probe_on_bus()`. The current descriptor decoder accepts only the defined Candis Dock rev. 1.1/1.2 identities.
- `candis_dock_probe_modules()` reports only whether the reserved U6/U7 EEPROM addresses acknowledge. An ACK does not establish a module identity or driver. Applications own module configuration and resource allocation.

## Defined resources

| Scope | Defined resources |
| --- | --- |
| Core board | TG28, RTC, touch and audio use I²C0. Reserve core display, USB and download resources. Core-only operation does not select any baseboard peripheral |
| Candis Dock | I²C1 uses GPIO58/59 and EEPROM U10 is at `0x50`. TF uses GPIO20–25 and card detect GPIO39. BMI270 connects to I²C0 through Q2 at `0x68`. CN3 uses the left channel on rev. 1.1 and the right channel on rev. 1.2 |
| Dock expansion headers | U6/U7 pin assignments and shared supplies are listed above. The BSP assigns no module model, functional GPIO, functional I²C address or driver |

GPIO39 connects both Dock card detect and U6.10. Keep the U6 side high impedance when using TF card detect. GPIO35 is shared by U6.4 and U7.19. Expansion I²C shares GPIO58/59 with UART0, so release UART0 before using `0x50`, `0x51` or `0x52`.

DC1SW supplies TF, U10, BMI270 and U6/U7. The TG28-ESP v1.0 datasheet rating is 300 mA. Include steady-state load, startup inrush and margin in the budget for the complete supply domain.

## Connection rules

Dock and U6/U7 expansion modules do not support live insertion or runtime rebinding. Disconnect USB, battery and external power before changing a baseboard, module or speaker. The application reads the configured assembly descriptor after startup and keeps each device instance bound for the running session.

## BSP support

- `candis_baseboard_decode()` validates the three CRCs and supported identity tuple of a 134-byte Dock descriptor.
- `candis_baseboard_probe_on_bus()` reads a descriptor on the caller-provided bus and address. `candis_baseboard_probe_state_on_bus()` returns the corresponding capability state.
- `candis_dock_probe()`, `candis_dock_get_state()` and `candis_dock_open()` use the fixed Dock I²C1 and `0x50`.
- `candis_dock_probe_modules()` probes only the reserved `0x51` and `0x52` addresses.

See [Candis Dock EEPROM](eeprom-v1.md) for descriptor fields and the Dock identities accepted by the current BSP.
