# Candis Dock

Dock expansion support for Candis hardware revision 1.2. The component adds
Dock card detection and SDMMC mount helpers to the main BSP. The
existing EEPROM identity is represented by `CANDIS_BASEBOARD_BB`.

For local development, add a `path` dependency on `components/candis_dock`
to the application manifest. The component resolves `candis` from the same
checkout. See `examples/expansion` for a buildable application.

Mate the Dock to both expansion connectors and supply its DC1SW domain.
Release UART0 on GPIO58/59 before probing the EEPROM. Include
`bsp/candis_dock.h` and pass the verified Dock descriptor to
`candis_dock_mount()`. Unmount with `candis_storage_unmount()` before
disconnecting the board or removing power.

Applications with a verified descriptor and measured expansion load can
use `candis_dock_mount_with_power()` to opt in to DC1SW control. The storage
driver shares its DC1SW lease with other active clients. GPIO39 must be
available for card detect during the mount. The U6 module socket shares
that line and must leave it available as an input.

Use `candis_dock_detect_start()` with the verified descriptor to keep a
reference to the powered expansion domain while polling the active-low
switch with `candis_storage_detect_read()`. Use
`candis_dock_mount_with_config()` with `read_only = true` for a directory
browser. Close directory and file handles, unmount, then release card
detection with `candis_storage_detect_stop()`.

The expansion example observes the Dock for 60 seconds. It debounces
insertions and removals, lists up to 16 entries from each mountable FAT
card, and unmounts after each scan.

For a writable test card, add `sdkconfig.tf_test` to the example's defaults.
The test creates an exclusive 4 KiB file, synchronizes it, reopens and verifies
the bytes, then removes that file. Wait for the final unmount log before
removing the card.

On a Dock with verified module wiring and I²C pull-ups, enable
`sdkconfig.modules` to report ACK at the expected `0x51` and `0x52` addresses.
Applications can call `candis_dock_probe_modules()` for the same snapshot.
Use the module specification and descriptor to identify its hardware.

Host tests cover descriptor parsing, mount policy, GPIO restoration and
release retries. The EEPROM and GPIO39 card-detect path still require
validation on the physical Dock.
