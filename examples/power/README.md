# Candis power monitor

The AMOLED displays supply and charging state, battery voltage, reported state of charge, and rail enables. Candis applies a 200 mA constant-current charge setpoint after checking TG28 identity, valid VBUS and battery presence, charger configuration, and fault status. The application verifies the register readback and retries the check when charger input becomes available after boot. Confirm the [panel supply](../../docs/en/hardware.md#power) before starting.

```sh
idf.py --preview -C examples/power build
```

Follow [USB and flashing](../../docs/en/usb.md) to install the application. The power page remains available when the battery is absent. The displayed charge current is a programmed target. Actual battery current requires measurement in the battery branch.

Enable `CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST` for a supervised touchscreen test that temporarily reduces the setpoint to 25 mA. When the test ends, the application applies the default setpoint once the charger passes the same checks. See the [TG28-ESP driver guide](../../docs/en/tg28-esp-coverage.md) for integration requirements.
