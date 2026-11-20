# Candis power monitor

The AMOLED displays supply and charging state, battery voltage, reported state of charge, and rail enables. The default build reads the PMU without changing charge settings. Confirm the [panel supply](../../docs/en/hardware.md#power) before starting.

```sh
idf.py --preview -C examples/power build
```

Follow [USB and flashing](../../docs/en/usb.md) to install the application. The power page remains available when the battery is absent. The displayed charge current is a programmed target. Actual battery current requires measurement in the battery branch.

Use the default read-only profile for monitoring. The optional charge test requires a qualified battery and supervised equipment with independent current and temperature cutoffs. A fixed TS resistor provides no cell temperature measurement. See the [TG28-ESP driver guide](../../docs/en/tg28-esp-coverage.md) for integration requirements.
