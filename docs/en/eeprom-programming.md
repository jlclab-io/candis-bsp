# Candis EEPROM programming

[Documentation](README.md) · [简体中文](../zh/eeprom-programming.md)

This guide applies to dedicated programming fixtures and authorized production or service stations. See [Candis EEPROM V1](eeprom-v1.md) for image fields, IDs, and CRCs. Application firmware reads descriptors during [cold-start discovery](expansion.md#cold-start-discovery).

## AT24C02 transactions

Dock rev. 1.2 and Gamepad use the SOP-8 AT24C02 (LCSC `C688857`) at U10. The part holds 256 bytes in 32 pages of 8 bytes and uses an 8-bit word address. Its maximum internal write time is 5 ms. Verify the actual part, temperature, and supply conditions against the datasheet. For substitutions, recheck the page size, address pins, write protection, timing, and voltage. [AT24C02 datasheet, sections 2 and 10–13](https://atta.szlcsc.com/upload/public/pdf/source/20200619/C688857_168265888F77B455D6D4136881F4BB15.pdf)

- **Device address.** Set the A2/A1/A0 straps with power off. Tie each to the EEPROM's VCC or GND. Pass the 7-bit address `0x50 | (A2<<2) | (A1<<1) | A0` to the I²C driver. The on-wire write/read control bytes for BTB, LEFT, and RIGHT are respectively `A0/A1`, `A2/A3`, and `A4/A5` in hex. See [locations and addresses](expansion.md#locations-and-addresses).
- **Random start, sequential read.** At 100 kHz, issue START, device address with W, and the 8-bit offset. Issue repeated START and device address with R. ACK each received byte except the last, then send NACK and STOP. Start at `0x00`. Read 134 bytes for discovery and up to 256 bytes for programming and backup. Stay within `0x00..0xFF`. The initial W phase sets the offset.
- **Page write.** Issue START, device address with W, offset, and page data. STOP starts the internal write. Write no more than `min(bytes_remaining, 8 - (offset % 8))` bytes per transaction. Crossing a page boundary wraps and overwrites bytes within that page. Compare existing data and skip matching pages. A write may span from the first to the last differing byte of a page using the original values for any unchanged bytes in between.
- **ACK polling.** After STOP, send START with the same 7-bit write address and end the probe. After NACK, wait briefly, for example 1 ms, and retry. After ACK, read back the bytes. Bound the operation to 30 ms from STOP and abort on timeout, bus fault, or mismatch. With fixtures that only support combined reads, wait the full 5 ms and use bounded readback within the page to verify readiness and data.
- **Write protect.** Pin 7 tied to VCC protects the entire array. GND permits writes. Dock rev. 1.2 and Gamepad hard-wire WP to ground, so the station and firmware restrict writes. Avoid driving the pin high on those boards. New modules should pull WP up to the EEPROM supply by default and allow the authorized fixture to pull it low. Confirm the voltage domain, restore protection after programming, and read back the data.

## Production workflow

1. **Generate the job and image.** Confirm the product registration revision, BOM, fixture, slot, 7-bit address, and assembly revision. Allocate a unique serial number and encode identity, date, batch, factory, and approved parameters. Fill the descriptor padding and calculate all three CRCs. Append 122 bytes of `FF` for a 256-byte production image. Independently decode the image and verify the fields, TLVs, CRCs, product, and ranges. Record its SHA-256.
2. **Confirm the target and back up.** Connect one physical device or one isolated branch at a time. Check the board, slot, pin 1, address straps, WP, voltage, and ground. Give the programmer exclusive access to I²C and isolate UART0 drivers and any other devices at the same address. For in-circuit work, verify Q3, DC1SW, total domain load, and backfeed limits. Archive a full 256-byte read. Continue as first programming only if all 256 bytes are `FF`. Route other contents or unidentified assemblies through the matching service job. Use the physical target identity at the station.
3. **Write in phases.** If the first 134 bytes already match the target, go directly to final verification. Otherwise, write zeros to `0x00..0x02` and verify the magic is invalid. Leave `0x03..0x07` untouched for now. Write only required segments of `0x08..0x85` within each page, with ACK polling and readback after each write. On page `0x80`, write at most the first six bytes to preserve `0x86..0x87`. Verify all of `0x08..0x85`, then commit the target `0x00..0x07` in one page write.
4. **Read back and release.** Restore write protection where supported. Read all 256 bytes. Compare the first 134 bytes to the approved image, validate the fields and CRCs, and verify the reserved region. Expect `FF` in that region on new blank devices and the saved contents during service. Power cycle and verify that the host picks the expected identity, slot, power, and driver configuration. Record the unit and serial, address straps, image and readback hashes, fixture, and result. Hold any mismatched unit for review.

Use `CND` in production only with supporting firmware. An existing `ESP` baseboard can continue through the [compatibility reader](eeprom-v1.md#validation-and-compatibility). To migrate it, archive the original image, generate a new image from the registered product and job, and verify the new fields and TLVs. [ESP-Mosaico programming example](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/5674ab1f4cc250350d02e0f03c409b1877c954ca/examples/module_eeprom_programmer)

## Service and recovery

- **Calibration update.** Read, archive, and validate the original image. Merge the approved TLVs and preserve other fields and unknown optional tags. Recalculate parameter length, padding, and its CRC. Compare the full target and follow the staged programming procedure. Keep the identity and manufacturing CRCs when their inputs are unchanged. Store normal preferences in host settings.
- **Interrupted write.** Hold the unit for revalidation. Restore stable power, isolate the target and read all 256 bytes. Resume the approved image or restore the archived image as specified by the job. Compare before writing, then compare and decode the full readback and verify a cold boot. Firmware leaves the driver disabled on invalid magic, a CRC fault, or a readback mismatch. Recover through the station.
- **Backups and endurance.** Two descriptors require 268 bytes, so each 256-byte part uses one descriptor and an external backup. Committing the first page last reduces the chance of using a partial image. Qualify power-loss behavior on the production fixture. Store images, full readbacks and hashes per unit. Skip writes to matching pages, bound retries, and record failures. Avoid writes during boot, polling, fault counting, and normal preference changes. Regenerate missing backups from traceable records or hold the unit when identity is uncertain.

## V1 test vectors

These offline reference descriptors use magic `CND`, Vendor `0xCA4E`, SW Compatibility `0x0100`, Flags `0`, and Parameter Version `1`. The decimal manufacture date is `20261120`, batch `5`, and factory `2`. Board Name is the product name shown below. Use the [resource profiles](expansion.md#resource-allocation) for wiring. Reserved products and reference modules require design and qualification before production.

| Board Name | Location and address | Type / Product ID | Hardware Revision | Serial Number | TLV and purpose |
| --- | --- | --- | --- | --- | --- |
| `candis_bb` | BTB `0x50` | `0x31 / 0x0003` | `0x0100` | `0x00030001` | Length 0, direct baseboard without slots |
| `candis_gamepad` | BTB `0x50` | `0x30 / 0x0001` | `0x0100` | `0x00010001` | 17 bytes, 90° clockwise rotation, X 110/1500/2840 mV and Y 125/1540/2950 mV |
| `candis_dock` | BTB `0x50` | `0x31 / 0x0002` | `0x0102` | `0x00020001` | Length 0, rev. 1.2 profile with right speaker channel and two slots |
| `candis_joystick` | LEFT `0x51` | `0x32 / 0x0101` | `0x0100` | `0x01010001` | 14 bytes, X 150/1500/2850 mV and Y 160/1550/2900 mV |
| `candis_sensor` | RIGHT `0x52` | `0x32 / 0x0102` | `0x0100` | `0x01020001` | 4 bytes, 2000 ms interval. The approved firmware profile supplies function address `0x44` |

| Board Name | Identity CRC | Manufacturing CRC | Parameter CRC |
| --- | --- | --- | --- |
| `candis_bb` | `0x133A` | `0x00C1` | `0xC619` |
| `candis_gamepad` | `0x594E` | `0x00C1` | `0xF3E6` |
| `candis_dock` | `0x6527` | `0x00C1` | `0xC619` |
| `candis_joystick` | `0x35E7` | `0x00C1` | `0x91BF` |
| `candis_sensor` | `0xFE75` | `0x00C1` | `0xC660` |

Each row starts with a hexadecimal offset, followed by bytes in increasing address order. Fill `0x86..0xFF` with `FF` for a full EEPROM image.

### Direct baseboard

```text
00  43 4E 44 31 03 00 00 01 00 01 4E CA 00 00 00 00
10  01 00 03 00 63 61 6E 64 69 73 5F 62 62 00 00 00
20  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30  00 00 00 00 3A 13 00 29 35 01 05 00 02 00 C1 00
40  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
50  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80  00 00 00 00 19 C6
```

### Gamepad

```text
00  43 4E 44 30 01 00 00 01 00 01 4E CA 00 00 00 00
10  01 00 01 00 63 61 6E 64 69 73 5F 67 61 6D 65 70
20  61 64 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30  00 00 00 00 4E 59 00 29 35 01 05 00 02 00 C1 00
40  01 00 11 00 01 01 01 02 0C 6E 00 DC 05 18 0B 7D
50  00 04 06 86 0B 00 00 00 00 00 00 00 00 00 00 00
60  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80  00 00 00 00 E6 F3
```

### Dock

```text
00  43 4E 44 31 02 00 02 01 00 01 4E CA 00 00 00 00
10  01 00 02 00 63 61 6E 64 69 73 5F 64 6F 63 6B 00
20  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30  00 00 00 00 27 65 00 29 35 01 05 00 02 00 C1 00
40  01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
50  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80  00 00 00 00 19 C6
```

### LEFT joystick

```text
00  43 4E 44 32 01 01 00 01 00 01 4E CA 00 00 00 00
10  01 00 01 01 63 61 6E 64 69 73 5F 6A 6F 79 73 74
20  69 63 6B 00 00 00 00 00 00 00 00 00 00 00 00 00
30  00 00 00 00 E7 35 00 29 35 01 05 00 02 00 C1 00
40  01 00 0E 00 02 0C 96 00 DC 05 22 0B A0 00 0E 06
50  54 0B 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80  00 00 00 00 BF 91
```

### RIGHT sensor

```text
00  43 4E 44 32 02 01 00 01 00 01 4E CA 00 00 00 00
10  01 00 02 01 63 61 6E 64 69 73 5F 73 65 6E 73 6F
20  72 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30  00 00 00 00 75 FE 00 29 35 01 05 00 02 00 C1 00
40  01 00 04 00 04 02 D0 07 00 00 00 00 00 00 00 00
50  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80  00 00 00 00 60 C6
```

For offline verification, decode each vector and compare its fields. Flip a byte in each of the three sections in turn to check CRC rejection. Cover unsupported versions, oversized or truncated TLVs, unknown optional tags, wrong addresses, and the legacy ESP path. On hardware, confirm strapping, complete readback, and recovery from interrupted programming.
