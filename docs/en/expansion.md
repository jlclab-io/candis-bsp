# Candis expansion hardware

[Documentation](README.md) · [简体中文](../zh/expansion.md)

The 30-pin CN1 and 50-pin CN2 on Candis mate with one Dock baseboard at a time. The BSP recognizes Dock rev. 1.1/1.2 as `CANDIS_BASEBOARD_BB` (EEPROM product ID 2). The EEPROM V1 layout is unchanged, and valid product identities outside the supported Dock revisions return `ESP_ERR_NOT_SUPPORTED`. Module discovery is limited to approved Dock profiles.

```text
Candis core board
  CN1 + CN2 → BTB 0x50 → One baseboard
                         └ candis_dock
                           ├ LEFT  (U6) → 0x51
                           └ RIGHT (U7) → 0x52
```

## EEPROM wiring

Dock rev. 1.2 uses the AT24C02 (`C688857`) at U10. A0/A1/A2 and WP are tied to GND. VCC is on DC1SW. U10's SDA and SCL connect to the expansion bus through Q3 (2N7002DW), with its gates and both 4.7 kΩ pull-ups tied to DC1SW. Dock U6/U7 share the expansion bus side of Q3. The core board has no baseboard descriptor EEPROM.

### BTB pinout

The core board uses `C41421082` at CN1 and `C41421084` at CN2. Dock rev. 1.2 uses the `C41421022` and `C41421024` mating connectors. The following mapping is based on schematic and PCB connectivity and connector orientation. Verify the mating drawing, pin 1, and continuity on the intended assembly before manufacturing. Multiple pins in a row match in the listed order. For example, core CN2.11/12 mate with baseboard CN2.15/14. Baseboard CN2.11/12 carry VBAT.

#### CN1

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

#### CN2

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

On Dock rev. 1.2, LEFT is U6 and RIGHT is U7. Both use a `C46635855` dual-row header. Confirm the reference designator and pin 1 before connecting a module.

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

The two slots directly share GPIO35, EXP_SCL, EXP_SDA, DC1SW, ALDO3, ALDO4, BLDO1, and ground. U6.10 also shares GPIO39 with TF card detect and its pull-up. The slots have a common supply domain without individual power switches, presence contacts, address select, or bus isolation. Q3 is on the U10 branch.

## Locations and addresses

| Location | 7-bit address | A2/A1/A0 | Basis |
| --- | --- | --- | --- |
| BTB U10 | `0x50` | `000` | Confirmed in the Dock rev. 1.2 design |
| Dock LEFT (U6) | `0x51` | `001` | Required by this specification. Strap on the module or adapter |
| Dock RIGHT (U7) | `0x52` | `010` | Required by this specification. Strap on the module or adapter |

The Dock connectors carry no EEPROM address-select signal. Strap each module's address pins to its EEPROM VCC or GND with power off. Verify its straps, WP, VCC, connector orientation, and actual response with one isolated module at a time, then check both slots together. Use validated keying or a dedicated adapter to prevent assembly errors.

A product can share its identity and driver across approved slots. Register the electrical and resource profile for each allowed location. The address straps follow the slot, the serial number follows the module, and the instance follows the slot and lifecycle generation. Pin positions differ between slots. A module used in either slot needs matching wiring or an adapter. The reference joystick currently specifies LEFT only.

Two devices at one address may both acknowledge and return data that appears valid. A module incorrectly strapped to `0x50` can interfere with its parent. On a collision, suspected mixed response, or mismatch with the assembly record, keep the affected devices and bus inactive. Report the location and address. Power off and verify the straps on isolated devices before retrying.

## Cold-start discovery

1. **Start the core board.** Keep dedicated expansion lines high impedance and defer conflicting functions. Enable discovery only for a known assembly and verified supply conditions. DC1SW powers TF, IMU, U10, and attached modules together. Their power-on defaults and combined load must be safe. Otherwise keep the core board active and report why probing is unavailable.
2. **Acquire expansion I²C.** Keep main I²C0 on GPIO6/7. Route I²C1 over the GPIO matrix to GPIO58 SCL and GPIO59 SDA, the native UART0 pins. Release UART0 and external drivers before claiming both open-drain lines. Keep a verified download path such as native USB OTG. Exclude concurrent access by other drivers or the programming fixture. Confirm DC1SW and its upstream supply are stable and SDA/SCL idle high before reading at 100 kHz.
3. **Discover the parent.** Read 134 bytes at offset `0x00` from `0x50`. Allow at most three attempts per address, with at least 5 ms between attempts recommended. Use a 30 ms probe timeout and a 40 ms combined read timeout. Require two consecutive identical reads before [V1 validation](eeprom-v1.md#validation-and-compatibility). Record stable NACK as no response and changing data or mixed ACK/NACK/timeouts as unstable. Stop probing on a stuck-low line or bus-level error.
4. **Discover slots on an approved Dock.** After validating its exact revision and assembly profile, read `0x51` and then `0x52` under the same rules. Keep module drivers off until both slots have been assessed together. Only registered addresses are scanned. Unidentified assemblies and the direct BB profile have no slot scan.
5. **Reserve resources and bind.** Review the topology against the active core functions. Record location, I²C controller and address, decoded identity, serial number, status, and generation. Bind handles to the generation. Reserve all resources before the configured power and reset sequence. Roll back resources acquired by a failed bind and keep core functions running.

| Discovery result | Default action |
| --- | --- |
| Probing disabled, DC1SW unstable, or UART0 still in use | Use the core board and report the condition |
| No response at `0x50` | Use the core board. Absence, missing EEPROM, power loss, or an open connection can all cause NACK |
| ACK with all `FF` or all `00` | Report a blank descriptor and leave the position unbound |
| Short read, invalid CRC or format, or changing data | Report the address and error and leave the position unbound |
| Unknown vendor, product, version, revision or variant, wrong position, or duplicate serial | Report the identity without binding the affected instances. Check nonzero serials within the same vendor and product |
| Slot absent or incompatible | Leave the slot unbound. Use other functions only when the approved assembly shows that its attached load will not interfere |
| Collision, stuck-low bus, or unexpected load | Stop activity on the affected domain, release the manager's resources safely, and cold boot after correcting the issue |

When discovery ends, remove temporary I²C handles before releasing probe pins and power. For a known baseboard without EEPROM, an operator may select a fixed assembly profile after checking the actual board and supply. The automatic path remains in core-only mode on no response. A single NACK cannot establish that U6 is empty for TF card detection.

## Resource allocation

Match the core and parent revisions, slot, and [product key](eeprom-v1.md#ids-and-hardware-revisions) to an approved profile. Record each function's pin direction, reset state, pull-ups, sharing rules, voltage domain, controller and address, supply dependencies, current, and shutdown order. The driver registry provides initialization and release operations. Apply optional EEPROM parameters only after binding the corresponding driver.

| Resource | Allocation and conflicts |
| --- | --- |
| GPIO and pin multiplexing | GPIO35 is shared across slots. GPIO39 is also connected to card detect. GPIO37 is shared with the core LED. Outputs and alternate functions are exclusive by default. Shared inputs require compatible domains, pulls, and semantics, with no conflicting output. Reserve core, reset, boot, recovery, and fixed board functions first. Confirm the GPIO33/34 USB Serial/JTAG and GPIO54–57 external JTAG assignments before reuse. VBAT, PWRON, NTC, VSYS, 5V, differential speaker outputs, and CHIP_PU are fixed signals |
| ADC | Reserve the verified GPIO, ADC unit, channel, and input mode. Record attenuation, range, and calibration. Keep the ADC pin free of digital outputs and other conflicting modes |
| I²C | Share the verified controller and pins. Reserve each (bus, 7-bit address) for one device. Expansion addresses `0x50..0x52` are reserved for descriptors. Check addresses, voltage, open-drain operation, parallel pull-ups, and capacitance for all powered devices. BMI270 `0x68` is on main I²C |
| SPI | Reserve an exclusive controller or approved shared clock/data lines with individual CS and agreed clock limits. Unselected devices release the return line. Reserve the core flash, PSRAM, and display resources first. Generic Dock SPI pin assignments are unregistered |
| UART | Reserve the controller, TX/RX, and any direction control. Verify the attached endpoint. I²C1 and UART0 on GPIO58/59 are mutually exclusive. Check power and line state before returning them to UART0. Generic Dock UART pin assignments are unregistered |
| Power | Reference-count common rails and lock their approved configuration. Use the TG28-ESP v1.0 table 6-3 **300 mA** DC1SW rating (default off) to budget U10, TF, IMU, and both slots, including steady state, inrush, and margin. Verify limits and voltage compatibility for other rails. Each holder releases only its own reference |

| Profile | Fixed uses and constraints |
| --- | --- |
| Core board | TG28, RTC, touch, and audio use I²C0. Reserve core display, storage, and download resources. Use core-only operation without an available baseboard |
| `candis_bb` | Reserved direct baseboard without slots. Identity uses `0x50`. Register its remaining resources from its own schematic and BOM |
| `candis_dock` rev. 1.2 | SDMMC uses the same pins and GPIO39 CD. BMI270 connects to I²C0 through Q2. CN3 carries the right differential speaker output. Rev. 1.1 carries the left output. Use only validated slot pin assignments and sharing arrangements |
| Reference joystick | LEFT U6.20/GPIO42 and U6.18/GPIO43 are X/Y, U6.6/GPIO36 is the press input. U6.11/DC1SW and GND supply the module. U6.17/15 are SCL/SDA. The only I²C device is EEPROM `0x51`. Verify ADC range and mode, pulls, and assembly keying |
| Reference sensor | RIGHT U7.16/18 are SCL/SDA, and U7.10/20 are DC1SW/GND. EEPROM uses `0x52`, with `0x44` as the example sensor address and no GPIO IRQ. Register the actual part, straps, timing, and load before use |

Both slots may operate together after addresses, electrical levels, pins, and the power budget have been validated. The reference LEFT joystick and RIGHT sensor form a proposed combination for qualification. A module that uses GPIO39 for its own function conflicts with TF card detect. Two drivers using GPIO35 are exclusive by default. Identical modules with different EEPROM addresses still need distinct functional device addresses or hardware isolation.

Reserve core, boot, recovery, and fixed board resources first. Resolve a remaining conflict by selecting a verified profile with non-selected devices electrically inactive. Keep conflicting devices unbound if the hardware cannot isolate their activity. On release, use the active generation to prevent stale callbacks from accessing released resources.

## Lifecycle

Disconnect USB, battery, and external power and allow the rails to discharge before changing an expansion assembly. Stop active sessions before disconnecting power. Firmware may stop and reread identities on the same assembly. An unexpected read error during operation can stop the affected driver. Rebinding in V1 occurs at cold start.

During shutdown, reject new requests, sync and unmount storage, complete or cancel bus and DMA work, disable function outputs and interrupts, and wait for callbacks to finish before releasing devices, pins, and power. Check that expansion I²C and external drivers have released the lines before enabling UART0.

Hot-plug support requires a hardware revision with per-slot detection, controlled power and current limiting, isolation for I²C and shared GPIO, reset sequencing, and verified connector contact order. Use insertion, removal, and load fault tests before enabling runtime rebinding. The [ESP-Mosaico module manager](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/5674ab1f4cc250350d02e0f03c409b1877c954ca/components/mosaico_module_mgr) is a reference for resource ownership and slot lifecycle.

## BSP support

- `candis_baseboard_probe()` explicitly checks the deployed `ESP` descriptor at `0x50` for supported Dock revisions. `candis_baseboard_probe_state()` and `candis_dock_get_state()` distinguish an identified but unopened Dock from an active component session. The application decides when to probe and which driver to start.
- `candis_dock_probe_modules()` reports address responses on `0x51` and `0x52`. The `CND` reader, child descriptor validation, and topology-wide resource allocation described here require host integration before use.
- Before deploying an assembly, verify connector orientation and continuity, matched revisions, address straps and WP, rail timing and combined load, and cold-start handling of blank or incompatible descriptors. Qualify the intended module combinations, shared pins, and resource release on the assembled board.
