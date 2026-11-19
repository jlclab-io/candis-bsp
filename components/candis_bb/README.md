# Candis BB

BB expansion support for Candis hardware revision 1.2. The component adds
the BB card-detect configuration to the main BSP's SDMMC mount API.

For local development, add a `path` dependency on `components/candis_bb`
to the application manifest. The component resolves `candis` from the same
checkout. See `examples/expansion` for a buildable application.

Mate the BB to both expansion connectors and supply its DC1SW domain.
Release UART0 on GPIO58/59 before probing the EEPROM. Include
`bsp/candis_bb.h` and pass the verified BB descriptor to
`candis_bb_mount()`. Unmount with `candis_storage_unmount()` before
disconnecting the board or removing power.

The descriptor parser is covered by host tests. The formal BB EEPROM and
its GPIO39 card-detect path still require validation on the physical BB.
