# Candis Dock

Dock expansion support for Candis hardware revision 1.2. The component adds
the Dock card-detect configuration to the main BSP's SDMMC mount API. The
existing EEPROM identity is represented by `CANDIS_BASEBOARD_BB`.

For local development, add a `path` dependency on `components/candis_dock`
to the application manifest. The component resolves `candis` from the same
checkout. See `examples/expansion` for a buildable application.

Mate the Dock to both expansion connectors and supply its DC1SW domain.
Release UART0 on GPIO58/59 before probing the EEPROM. Include
`bsp/candis_dock.h` and pass the verified Dock descriptor to
`candis_dock_mount()`. Unmount with `candis_storage_unmount()` before
disconnecting the board or removing power.

The descriptor parser is covered by host tests. The EEPROM and GPIO39
card-detect path still require validation on the physical Dock.
