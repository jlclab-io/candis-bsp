# TG28-ESP monitor

A standalone read-only example for the [TG28-ESP component](../../components/tg28-esp/README.md). The default I²C settings match Candis: SCL GPIO6, SDA GPIO7, 7-bit address `0x34`, and 100 kHz. On custom hardware, select the bus wiring in **TG28-ESP monitor** under `menuconfig` and provide suitable pull-ups.

```sh
idf.py --preview -C examples/tg28 build
```

Follow [USB and flashing](../../docs/en/usb.md) to install the application. Open the console to read the supply and charger state, rail settings, ADC readings and latched events. The default monitor reports a finite set of samples and releases the bus. Use the [Candis power monitor](../power/README.md) for the AMOLED interface.
