# Candis Dock EEPROM descriptor

[Documentation](README.md) · [简体中文](../zh/eeprom-v1.md)

Candis Dock stores its board identity in the AT24C02 at U10. Dock rev. 1.1 and 1.2 use the same 134-byte descriptor and leave the rest of the device reserved. The Dock address is `0x50`. The U6 and U7 module EEPROM addresses are `0x51` and `0x52`. See [expansion hardware](expansion.md#locations-and-addresses) for address and supply wiring.

## Wire format

The descriptor occupies EEPROM offsets `0x00..0x85`. Encode unsigned multi-byte fields in little-endian order. The magic is ASCII `ESP`. The current BSP accepts only the fixed Candis Dock identity tuple.

| Offset | Size (bytes) | Type | Field | Current definition |
| ---: | ---: | --- | --- | --- |
| `0x00` | 3 | byte[3] | Magic | ASCII `ESP`, bytes `45 53 50` |
| `0x03` | 1 | u8 | Board Type | `0x31` for Dock |
| `0x04` | 2 | u16 | Board ID / Product ID | `0x0002` for Candis Dock |
| `0x06` | 2 | u16 | Hardware Revision | `0x0101` for rev. 1.1 and `0x0102` for rev. 1.2 |
| `0x08` | 2 | u16 | SW Compatibility | `0x0100` |
| `0x0A` | 2 | u16 | Vendor ID | `0xCA4E` for Candis |
| `0x0C` | 4 | u32 | Board Flags | Only bit 0 is accepted. All other bits are zero |
| `0x10` | 4 | u32 | Serial Number | Manufacturing serial within the product |
| `0x14` | 32 | byte[32] | Board Name | Printable ASCII padded on the right with `0x00` |
| `0x34` | 2 | u16 | Identity CRC | Covers `0x00..0x33` |
| `0x36` | 4 | u32 | Manufacture Date | Decimal `YYYYMMDD` |
| `0x3A` | 2 | u16 | Batch Number | Manufacturing batch |
| `0x3C` | 2 | u16 | Factory ID | Manufacturing factory |
| `0x3E` | 2 | u16 | Manufacturing CRC | Covers `0x36..0x3D` |
| `0x40` | 2 | u16 | Parameter Version | `0x0001` |
| `0x42` | 2 | u16 | Parameter Length | `0..64` bytes |
| `0x44` | 64 | byte[64] | Parameter Data | The BSP checks Parameter Length and this section's CRC. It does not interpret the data |
| `0x84` | 2 | u16 | Parameter CRC | Covers `0x40..0x83` |
| `0x86` | 122 | byte[122] | Reserved EEPROM Area | Outside the descriptor. The current BSP ignores this area |

All three sections use **CRC-16/MODBUS**. The polynomial is `0x8005`, the right-shift implementation uses `0xA001`, the initial value is `0xFFFF`, and the final XOR is `0x0000`. Start each section at the initial value. Its CRC field is outside its coverage. Store each result low byte first. ASCII `123456789` produces `0x4B37`, stored as `37 4B`.

## Supported identities

| Vendor ID | Board Type | Product ID | Product | Hardware Revision |
| --- | --- | --- | --- | --- |
| `0xCA4E` | `0x31` | `0x0002` | Candis Dock | `0x0101`, `0x0102` |

`candis_baseboard_decode()` accepts only the identity in the table. The Dock address is fixed at `0x50`. `0x51` and `0x52` are reserved module EEPROM addresses for U6 and U7. The current BSP does not bind a product, functional device or driver to either module address.

## Validation order

1. Read all 134 descriptor bytes.
2. Require the `ESP` magic.
3. Validate the Identity, Manufacturing and Parameter CRCs independently.
4. Require Parameter Length to be at most 64.
5. Validate Vendor ID, SW Compatibility, Parameter Version and Board Flags.
6. Require Board Type, Product ID and Hardware Revision to match the supported identity table.

If any step fails, the decoder returns no identity. A complete device image is 256 bytes and `0x86..0xFF` lies outside the descriptor. The BSP reads the descriptor and does not provide an EEPROM write API.

## Format reference

Candis Dock uses the ESP-Mosaico V1 field offsets, three CRC sections and 64-byte parameter area. Candis limits bindable hardware with the fixed Vendor ID, Board Type, Product ID and hardware revisions. [ESP-Mosaico module manager](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/5674ab1f4cc250350d02e0f03c409b1877c954ca/components/mosaico_module_mgr)
