# TG28-ESP

An ESP-IDF 6.2 I²C driver for TG28-ESP power management. It exposes supply and charge telemetry, regulator settings, charging configuration, interrupt handling, and power sequencing.

## Use in an application

Add a dependency on `components/tg28-esp` and include [`tg28_esp.h`](include/tg28_esp.h). Create the I²C master bus first.

```c
#include "tg28_esp.h"

esp_err_t open_power_monitor(tg28_esp_t *pmu, i2c_master_bus_handle_t bus)
{
    const tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    return tg28_esp_open(pmu, bus, &config);
}
```

Pass a zero-initialized device object. Opening registers the I²C device and preserves OTP and reset settings. Check the device with `tg28_esp_check_chip_id()`, read its configuration, and call `tg28_esp_close()` before removing the bus.

Serialize each operation to the same PMU from task context. Match supply limits, charge settings, and rail sequencing to the board and battery. Protection changes and reset-capable operations require an explicit open option. Validate actual voltage, battery current, and temperature on the assembled system.

[Driver guide](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/tg28-esp-coverage.md) · [Read-only monitor](https://github.com/jlclab-io/candis-bsp/blob/main/examples/tg28/README.md) · [Apache-2.0](LICENSE)
