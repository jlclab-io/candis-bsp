# TG28-ESP 驱动指南

[使用文档](README.md) · [English](../en/tg28-esp-coverage.md)

[`tg28-esp` 组件](../../components/tg28-esp/README.md)接入应用提供的 ESP-IDF I²C 主总线。打开设备时保留芯片的 OTP 和复位设置，修改前先读取实际配置。对同一 PMU 的访问由应用串行化，读改写操作需要覆盖整个调用。

| 接口 | 功能 |
| --- | --- |
| 状态与遥测 | 芯片 ID、上下电来源、充电状态、ADC、电量及按板级曲线换算 TS 温度 |
| 稳压器 | 电源轨使能、可调输出电压、DCDC 工作模式与 DC1SW/DC4SW 使能 |
| 充电 | 输入限制、充电电流和电压、终止、电池检测、后备充电与 TS/JEITA 设置 |
| 事件与时序 | IRQ 掩码、状态和应答、PWRON、PWROK、快速上电、睡眠唤醒与看门狗 |
| 辅助功能 | 暂存区、GPIO1 开漏及原始读取 |

## 集成要求

- 按电路和电池规格配置电源限制及时序。充电电流和稳压器电压回读为设定值。实际电池支路电流和电源轨电压须在装配好的硬件上测量。
- TS 温度换算需要 NTC 和正确的板级曲线。采用固定 TS 电阻的 Candis 装配无法测量电芯温度。
- 统一处理共用 IRQ 的中断源，只应答应用已经处理的事件。进入 PMU 睡眠前协调 SoC 并保持必要的电源轨。
- 更改保护配置和使用可触发复位的操作时，打开设备需设置 `allow_protection_changes`。在实际板卡上验证充电、关断、恢复及故障处理。
- GPADC 采样、写入 TS 模拟数据和写入电量计模型暂不支持。

接口以 [`tg28_esp.h`](../../components/tg28-esp/include/tg28_esp.h) 为准。使用示例见[独立 TG28 监测](../../examples/tg28/README.md)和 [Candis 电源监测](../../examples/power/README.md)。
