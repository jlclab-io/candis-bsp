# Candis Dock

Support for Candis Dock revisions 1.1 and 1.2. The component identifies the baseboard, manages a DC1SW reference, supports TF cards, and selects the CN3 speaker channel.

## Hardware

| Interface | Connection |
| --- | --- |
| TF card | Four-bit SDMMC on GPIO20–25. Card detect is active-low on GPIO39, shared with U6.10 |
| Expansion I²C | SCL GPIO58 and SDA GPIO59. The baseboard EEPROM is at `0x50` |
| DC1SW | Shared by TF, EEPROM, BMI270, and the two expansion headers. Budget total load and inrush within the rail's 300 mA rating |
| CN3 speaker | Differential left output on rev. 1.1, right on rev. 1.2 |
| CN4 battery | VBAT and GND. The fixed TS resistor provides no cell temperature measurement |

Use both BTB connectors. Release UART0 before claiming GPIO58/59, and leave U6 empty or keep its GPIO39 high impedance when using TF. See the [connector pinout](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/expansion.md) for expansion-header wiring.

## Use in an application

Add a dependency on `components/candis-dock` and include [`bsp/candis_dock.h`](include/bsp/candis_dock.h). Use one control task for the Dock session.

Call `candis_dock_get_state()` after the expansion rail is available to distinguish an absent Dock, a recognized but unopened Dock, and an initialized Dock session. `candis_dock_open()` owns the component's application DC1SW lease.

1. Use `candis_dock_open(false, &identity)` with DC1SW already powered. Opt into managing the rail only after verifying the complete load.
2. Use `candis_dock_detect_start()` to claim card detection and `candis_dock_mount_with_config()` to choose the mount policy. Use `candis_dock_get_speaker_channel()` to map CN3 to left or right PCM.
3. Close files, then call `candis_storage_unmount()`, `candis_storage_detect_stop()`, and `candis_dock_close()` for resources acquired during the session. Retry a failed release before removing power.

Use `candis_dock_read_eeprom()` for a read-only diagnostic copy of the raw descriptor while the Dock session is initialized.

[Dock example](https://github.com/jlclab-io/candis-bsp/blob/main/examples/dock/README.md) · [Changelog](CHANGELOG.md) · [Apache-2.0](LICENSE)
