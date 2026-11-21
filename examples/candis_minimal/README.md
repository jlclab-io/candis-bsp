# Minimal Candis example

```sh
idf.py --preview -C examples/candis_minimal build
```

The example needs only the Candis core board. It demonstrates an idempotent BSP I²C reference, a separate client reference, and recovery when ESP-IDF refuses to delete an I²C bus with a device still registered. It performs no I²C transfers and leaves the bus released.

Build, flash, and view the application console. Look for `CANDIS_I2C_LIFECYCLE_OK`. See [USB and flashing](../../docs/en/usb.md).

本示例仅用 Candis 核心板。它演示标准 I²C 启停、独立引用、设备仍注册时的释放失败，以及移除设备后的重试。程序不会发起 I²C 传输。构建、烧录后查看应用控制台，检查 `CANDIS_I2C_LIFECYCLE_OK`。安装方式见[USB 与烧录](../../docs/zh/usb.md)。
