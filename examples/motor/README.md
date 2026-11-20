# Candis motor example

Use the fitted M1 motor on the Candis evaluation fixture and connect a stable USB supply. Build the example and follow [USB and flashing](../../docs/en/usb.md).

```sh
idf.py --preview -C examples/motor build
```

Send one command per line on the application CDC console.

| Command | Action |
| --- | --- |
| `STATUS` | Read the motor rail state |
| `PULSE 75` / `PULSE 100` | Run a pulse for up to 600 ms at the selected duty percentage |
| `STOP` | End the pulse and restore the prior BLDO2 setting |

The output also stops and restores power when the pulse times out.
