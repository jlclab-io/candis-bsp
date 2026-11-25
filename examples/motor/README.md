# Candis motor example

Use a compatible motor driver assembly. The default configuration demonstrates GPIO5 with BLDO2 supplying the driver rail. Verify the pinout, supply range, driver polarity, and motor limits for the actual assembly before enabling output. The AMOLED UI provides start/stop, live amplitude control from 1–100, and a 1–20 kHz PWM carrier control. The console exposes the same controls for scripted checks. Build the example and follow [USB and flashing](../../docs/en/usb.md).

```sh
idf.py --preview -C examples/motor build
```

Send one command per line on the application CDC console.

| Command | Action |
| --- | --- |
| `STATUS` | Read battery, duty, amplitude, frequency, and cleanup state |
| `RUN 1-100` | Start continuous vibration at an amplitude |
| `AMP 1-100` | Change amplitude while running |
| `FREQ 1000-20000` | Change the PWM carrier frequency while running |
| `PULSE 1-100` | Run a 600 ms calibrated-amplitude pulse |
| `STOP` | Stop continuous output, cancel a pulse, or retry cleanup |

The output also stops and restores power when a pulse times out.
