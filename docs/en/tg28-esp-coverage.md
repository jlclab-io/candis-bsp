# TG28-ESP driver guide

[Documentation](README.md) · [简体中文](../zh/tg28-esp-coverage.md)

The [`tg28-esp` component](../../components/tg28-esp/README.md) attaches to an application-supplied ESP-IDF I²C master bus. Opening a device preserves its OTP and reset settings. Read the actual configuration before changing it. The caller serializes accesses to the PMU, including complete read-modify-write operations.

| Interface | Capabilities |
| --- | --- |
| Status and telemetry | Chip ID, power sources, charge state, ADC, state of charge, and TS conversion using a board-supplied curve |
| Regulators | Rail enable and voltage on adjustable outputs, DCDC operating modes, and DC1SW/DC4SW enables |
| Charging | Input limits, charge current and voltage, termination, battery detection, backup charging, and TS/JEITA settings |
| Events and sequencing | IRQ masks, status and acknowledgement, PWRON, PWROK, fast power-up, sleep and wake, and watchdog |
| Utilities | Scratch storage, GPIO1 open-drain output, and raw reads |

## Integration

- Match power limits and sequencing to the circuit and the battery. Charge current and regulator voltage settings are programmed targets. Measure battery branch current and rail voltage on the assembled hardware.
- A TS temperature conversion requires an NTC and the correct board curve. Candis assemblies with a fixed TS resistor provide no cell temperature measurement.
- Service shared IRQ sources together. Acknowledge only the events the application has handled. Coordinate PMU sleep with the SoC and keep the required rails powered.
- Protection changes and reset-capable operations require `allow_protection_changes` at open. Qualify charging, shutdown, recovery, and fault behavior for the actual board.
- GPADC sampling, TS override writes, and fuel-gauge model writes remain outside the supported scope.

See [`tg28_esp.h`](../../components/tg28-esp/include/tg28_esp.h) for function contracts, [TG28 monitor](../../examples/tg28/README.md) for read-only use, and [Candis power monitor](../../examples/power/README.md) for the AMOLED example.
