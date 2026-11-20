# USB and flashing

[Documentation](README.md) · [简体中文](../zh/usb.md)

Candis uses the onboard USB-C connector for native USB OTG. The examples normally expose a USB CDC serial console after boot. During ROM download and after an application restart, the host may enumerate a different serial port.

## First installation

1. [Build an example](../../README.md#quick-start), connect a data-capable USB cable, and close any terminal using the board's serial port.
2. Enter ROM download mode. If the board is on or flashing has failed, hold **PWRON** for **6 seconds** to power off. Hold **BOOT** and press **PWRON** for **2 seconds** to power on. Release **BOOT** and identify the ESP32-S31 ROM port on the host. The evaluation fixture also supports installation and recovery over its separate USB Serial/JTAG interface.
3. Use ESP-IDF to flash the full project. Replace `/dev/ttyACM0` with the verified ROM port.

   ```sh
   idf.py --preview -C examples/display -p /dev/ttyACM0 flash
   ```

4. Boot normally and open the application CDC port in a serial terminal. If the ROM port remains active, release **BOOT** and power-cycle the board.

Use full flashing for a first installation or a deliberate partition layout change. Back up any data you need before changing the layout.

## Application-only updates

Use [`candis_otg_flash.py`](../../tools/candis_otg_flash.py) with a compatible Candis example already running over native OTG CDC, or with the board already in USB OTG ROM mode.

1. Build the new application and close the serial terminal.
2. Note the target board's USB serial number (six hexadecimal bytes).
3. Run the command in your ESP-IDF environment from the repository root, with the actual serial and build directory.

```sh
python3 tools/candis_otg_flash.py --serial YOUR_BOARD_SERIAL --build-dir examples/display/build
```

The tool checks the board and installed partition table and writes only a compatible, unencrypted factory application partition. For a different installed layout, supply the verified partition-table binary with `--installed-table`, or use the full ESP-IDF flashing procedure if migration is intended.

## If the port does not appear

- Check the USB data cable and reconnect after reboot. ROM download and application CDC can have different port names.
- If application CDC is unavailable, use the BOOT and PWRON sequence under [First installation](#first-installation). Use the evaluation fixture's USB Serial/JTAG interface when available.
- The **Candis example USB** menu in `idf.py menuconfig` controls native CDC and automatic download. When exporting an example, copy [`candis_example_usb`](../../examples/common/candis_example_usb) to its `components/` directory using [`copy_example_usb.sh`](../../tools/copy_example_usb.sh).

For a standalone copy, update its application manifest to resolve the BSP dependencies. If the manifest uses [`candis_example_tf`](../../examples/common/candis_example_tf), copy that helper too and update its Candis dependency.
