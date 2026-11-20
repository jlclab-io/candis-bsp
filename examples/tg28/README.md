# 独立 TG28-ESP 监测

本示例使用独立的 `tg28-esp` 驱动和示例 OTG 控制台，驱动可独立于 USB 使用。
默认设置对应 Candis 主 I²C 的 SCL GPIO6、SDA GPIO7、
七位地址 0x34 及 100 kHz。第三方板在 `menuconfig` 的 **TG28-ESP monitor** 中选择引脚，
并按板级供电配置 I²C 上拉。目标默认 ESP32-S31。以下命令在示例目录中运行。

```sh
idf.py --preview build
```

启动后先核对 ID 0x4A，再输出上下电原因、看门狗、IRQ 使能、各轨开关和设定电压。
每秒采样一次供电状态、充电设定、电量、已启用 ADC 和锁存 IRQ，默认 20 组后释放设备和总线。
确认串口最后一行显示 `samples=20 monitor=ESP_OK cleanup=ESP_OK`，并根据实际输入和电池核对
读数。供电和电池状态由芯片报告，电压设定值及恒流目标需要另行用仪表核对。

默认运行不改变任何 PMU 寄存器。IRQ 只读，适合在按键或插拔测试中观察已启用事件。
在允许写暂存区的专用台架上，可启用 `TG28_EXAMPLE_SCRATCH_TEST`，写入测试值、回读比对，
再还原并复核四个字节。完整 Candis 显示与电源集成流程见 [电源示例](../power/README.md)。
