# Candis EEPROM V1

[Documentation](README.md) · [简体中文](../zh/eeprom-v1.md)

BTB baseboards and Dock modules use the same descriptor for identity, hardware revision, manufacturing information, and optional parameters. The host matches the descriptor to an approved firmware configuration for drivers, pins, and power. See [expansion hardware](expansion.md) for wiring and discovery, and [EEPROM programming](eeprom-programming.md) for production and binary test vectors.

This specification defines new images and validation rules. See [BSP support](expansion.md#bsp-support) for deployed firmware compatibility.

## Wire format

**Candis V1 uses the `CND` magic, a 134-byte descriptor, and a 256-byte EEPROM.** The descriptor occupies offsets `0x00..0x85`. Offsets are byte addresses. Encode unsigned multi-byte fields in little-endian order at the listed offsets.

| Offset | Size (bytes) | Type | Field | Meaning and range |
| ---: | ---: | --- | --- | --- |
| `0x00` | 3 | byte[3] | Magic | ASCII `CND`, bytes `43 4E 44` |
| `0x03` | 1 | u8 | Board Type | `0x30` direct Gamepad class, `0x31` direct general-purpose baseboard, `0x32` Dock module. Other values are reserved in V1 |
| `0x04` | 2 | u16 | Board ID / Product ID | One vendor-scoped product number, `0x0001..0xFFFE`. `0x0000` and `0xFFFF` are invalid |
| `0x06` | 2 | u16 | Hardware Revision | High byte major `1..254`, low byte minor `0..254`. For example, `0x0102` is rev. 1.2. Byte value `0xFF` is reserved |
| `0x08` | 2 | u16 | SW Compatibility | `0x0100` for V1, meaning descriptor compatibility version 1.0 |
| `0x0A` | 2 | u16 | Vendor ID | Candis uses `0xCA4E`. See the allocation rules below |
| `0x0C` | 4 | u32 | Board Flags | All 32 bits reserved in V1. Write zero. Product configuration defines features |
| `0x10` | 4 | u32 | Serial Number | `1..0xFFFFFFFE`, unique per vendor and product across batches and revisions. `0` means unassigned and is excluded from production images. `0xFFFFFFFF` is invalid |
| `0x14` | 32 | byte[32] | Board Name | 1–32 printable ASCII bytes `0x20..0x7E`. Pad on the right with zeros. Exactly 32 bytes needs no terminator. For display and reporting |
| `0x34` | 2 | u16 | Identity CRC | Covers `0x00..0x33`, including Magic |
| `0x36` | 4 | u32 | Manufacture Date | Decimal `YYYYMMDD` encoded as an integer. Use a valid Gregorian date in 2000–2099. `0` means unknown. Production images require a date |
| `0x3A` | 2 | u16 | Batch Number | `1..0xFFFE` per vendor, factory, year, and product. `0` means unknown. Production images require a batch. `0xFFFF` is invalid |
| `0x3C` | 2 | u16 | Factory ID | `1..0xFFFE` per vendor. `0` means unknown. Production images require a factory ID. `0xFFFF` is invalid |
| `0x3E` | 2 | u16 | Manufacturing CRC | Covers `0x36..0x3D` |
| `0x40` | 2 | u16 | Parameter Version | `0x0001` for V1 |
| `0x42` | 2 | u16 | Parameter Length | `0..64`, counting only valid TLV bytes. Default is `0` |
| `0x44` | 64 | byte[64] | Parameter Data | The first Length bytes hold TLVs. Fill the remainder with `0x00` |
| `0x84` | 2 | u16 | Parameter CRC | Covers `0x40..0x83`, including version, length, and zero padding |
| `0x86` | 122 | byte[122] | Reserved EEPROM Area | Outside the descriptor. Fill new 256-byte images with `0xFF` here. Readers ignore it. Preserve it during service |

All three sections use **CRC-16/MODBUS**. The polynomial is `0x8005` (`0xA001` in a right-shift implementation), with reflected input and output, initial value `0xFFFF`, and final XOR `0x0000`. Start each section at the initial value. Its CRC field is outside its coverage. Store each result low byte first. ASCII `123456789` produces `0x4B37`, stored as `37 4B`.

## IDs and hardware revisions

| Vendor ID | Allocation |
| --- | --- |
| `0xCA4E` | Candis, with product numbers maintained in this registry |
| `0x1000..0x7FFF` | One ID per third-party vendor, assigned by Candis maintainers |
| `0xFFFE` | Development fixtures. Explicit registration in development configurations only |
| Other values | Reserved. Native ESP-Mosaico descriptors have their own namespace |

To register a third-party product, provide the vendor, product, hardware revision, assembly variant, schematic, BOM, supported location, addresses, reset pin states, pin and bus requirements, and steady-state and inrush current. Candis maintainers assign the Vendor ID. A vendor assigns Product IDs within its own Vendor ID and registers the Board Type and supported revisions. Retired IDs remain reserved. Renaming preserves the ID. Changes to electrical interface class or product identity require a new product number. Changes to wiring, levels, power-on defaults, components, or driver compatibility require a hardware revision and validation of its firmware configuration.

| Vendor ID | Board Type | Product ID | Product | Registration |
| --- | --- | --- | --- | --- |
| `0xCA4E` | `0x30` | `0x0001` | `candis_gamepad` | Rev. 1.0 (`0x0100`), direct |
| `0xCA4E` | `0x31` | `0x0002` | `candis_dock` | Rev. 1.1 (`0x0101`) and 1.2 (`0x0102`), direct. The BSP enum uses `CANDIS_BASEBOARD_BB` |
| `0xCA4E` | `0x31` | `0x0003` | `candis_bb` | Reserved direct baseboard without module slots. Example rev. 1.0. Board design, BOM, and driver configuration require registration |
| `0xCA4E` | `0x32` | `0x0101` | `candis_joystick` | Reference joystick module, example rev. 1.0. Design and qualification pending |
| `0xCA4E` | `0x32` | `0x0102` | `candis_sensor` | Reference I²C sensor module, example rev. 1.0. Design and qualification pending |

Board Type `0x31` denotes an interface class. The host enumerates modules only if the approved product profile declares slots. The profile supplies the function class and features of each module. A registered `(Vendor ID, Product ID)` must match the reported Board Type before binding.

The firmware profile key is `(Vendor ID, Board Type, Product ID, Hardware Revision, Assembly Variant)`. Also check the parent board, physical interface, and resources for the assembly. Match explicitly validated revisions or registered equivalents. Use the serial number for traceability and attach the physical location and lifecycle generation to the driver instance.

## Parameter TLV

Each entry is `Tag:u8, Length:u8, Value:byte[Length]`, with no alignment or terminator. Tag is `0x01..0x7F` and Length is `1..62`. The entire entry must fit within Parameter Length. Each tag appears at most once. Writers emit ascending tags and readers accept any order. Reserved tags `0x00` and `0x80..0xFF` invalidate the parameter section. Readers skip unknown valid tags, and service tools preserve them when merging changes.

| Tag | Value length | Encoding and range | Default and purpose |
| --- | ---: | --- | --- |
| `0x01` | 1 | u8, `0..3` clockwise quarter turns from native display orientation | Default `0`. Used only by profiles that declare display layout support |
| `0x02` | 12 | Six little-endian u16 values in mV. X min/center/max followed by Y min/center/max. Each axis obeys `0 ≤ min < center < max ≤ 3300` | Use validated product calibration when absent. Actual limits depend on the board, ADC settings, and calibration |
| `0x03` | 2 | Little-endian u16 Assembly Variant, `0..0xFFFE`. `0xFFFF` is reserved | Default `0`. Register a nonzero variant for the exact product and revision. It selects a complete approved profile |
| `0x04` | 2 | Little-endian u16 preferred sensor sampling interval, `250..60000` ms | Reference sensor default is 1000 ms. Scheduling must respect the registered device's measurement constraints |

Candis maintainers register `0x05..0x3F`. Vendors manage `0x40..0x7F` within their Vendor ID. TLVs contain bounded parameters. The firmware profile supplies GPIOs, controller selection, device addresses, voltage and current limits, and write permissions. Product profiles use only the parameters they declare. A malformed parameter section prevents binding. If an optional parameter is absent and the profile has no verified default, disable the feature that requires it.

Store user-adjustable preferences, such as orientation or sampling interval, in host settings. Treat magic, versions, identity, flags, serial, name, manufacturing fields, and assembly variant as factory read-only after production. Authorized calibration stations may update approved tags using the [service procedure](eeprom-programming.md#service-and-recovery).

## Validation and compatibility

1. Read all 134 bytes. An all-`FF` or all-`00` descriptor is blank. Select the layout by magic. Check all three CRCs, the compatibility version, parameter version, and length. Decode the same descriptor region in a 256-byte production image.
2. For `CND`, check reserved bits, field ranges, name, date, padding, TLV boundaries, and known values. Report identity and reason for an unregistered vendor or product, unsupported revision or variant, or location mismatch. A passing CRC checks data integrity. Driver binding also requires the approved assembly and firmware profile.
3. At BTB `0x50`, accept deployed `ESP` magic (`45 53 50`) only for Vendor `0xCA4E`. The eligible Type/Product/Revision tuples are `0x30/0x0001/0x0100`, `0x31/0x0002/0x0101`, and `0x31/0x0002/0x0102`. Require all three CRCs, compatibility `0x0100`, parameter version `1`, and length ≤64. Bit 0 is allowed in legacy Board Flags and the other bits must be zero. Legacy names, manufacturing fields, and parameters are informational. Use firmware defaults for that revision. Other `ESP` descriptors and `ESP` in either Dock slot have no binding path.
4. Keep the original error when a `CND` descriptor fails. Use the compatibility reader only for actual `ESP` magic. Generate `CND` images for new production with firmware that supports them. Existing `ESP` baseboards can remain programmed as-is when the host supports the compatibility reader.

Compatible optional tags can be added within V1. Incompatible changes to the layout, field meaning, or mandatory parameters require a new compatibility version and reader. V1 readers reject unknown versions. Manage hardware revisions, firmware releases, and parameter versions independently.

## Format reference

Candis uses the field offsets, three CRC sections, and 64-byte parameter area of ESP-Mosaico V1. The `CND` magic identifies the Candis ID and TLV rules. [ESP-Mosaico module manager](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/5674ab1f4cc250350d02e0f03c409b1877c954ca/components/mosaico_module_mgr)
