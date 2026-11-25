# Candis Dock EEPROM reading and maintenance

[Documentation](README.md) · [简体中文](../zh/eeprom-programming.md)

Dock rev. 1.2 uses an AT24C02 SOP-8 at U10 (`C688857`). The device has 256 bytes in 32 pages, an 8-bit word address, 8-byte pages and a maximum 5 ms internal write cycle. See [Candis Dock EEPROM descriptor](eeprom-v1.md) for fields and CRC definitions.

## Address and access

U10 ties A0/A1/A2 and WP to ground, so its 7-bit address is fixed at `0x50`. The module EEPROM addresses for U6 and U7 are reserved as `0x51` and `0x52`. The current BSP parses only the Dock descriptor at `0x50`. `candis_dock_probe_modules()` reports only ACK status at `0x51` and `0x52`.

The BSP performs a random-address sequential read. The controller sends START, the device write address and an 8-bit memory address, followed by a repeated START and the device read address. Reading starts at `0x00`, and the Dock descriptor is 134 bytes. A transaction must not wrap past `0xFF`.

Dock rev. 1.2 hard-wires WP to ground. The BSP has no EEPROM write API and performs read-only access during normal operation. Production writes use a controlled offline or station workflow.

## Write rules

- Each page write stays within one 8-byte page. Split data across page boundaries. Writes that cross a boundary wrap within the device and overwrite bytes in the current page.
- After STOP, wait for the internal write cycle. The next transaction may use ACK polling, and a target-byte readback provides final confirmation.
- Back up the complete 256-byte image before writing and compare every byte after writing.
- Fill `0x86..0xFF` with `0xFF` in a new blank image. Preserve existing data in this area during service unless the work order explicitly changes it.
- Vendor ID, Board Type, Product ID, Hardware Revision, Serial Number, manufacturing fields and Parameter Data come from the approved Dock product record.

## Image approval

1. Generate the first 134 bytes using the [descriptor definition](eeprom-v1.md). Dock rev. 1.1 uses Hardware Revision `0x0101`, and rev. 1.2 uses `0x0102`.
2. Compute the Identity, Manufacturing and Parameter CRCs independently. Parameter Length must not exceed 64.
3. Build the complete 256-byte device image and verify its fields and CRCs with an independent parser.
4. After writing, read all 256 bytes and confirm that the first 134 bytes match the approved image, the reserved area matches the work order, and all three CRCs pass.
5. On a cold start, call `candis_dock_probe()` or `candis_baseboard_probe_on_bus()` and confirm a successful result with the expected hardware revision.

The upper-layer product identity and application configuration of a U6/U7 module belong to that module's board definition. The current BSP does not parse descriptors at either address or convert an ACK into a module driver binding.
