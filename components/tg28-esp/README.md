# TG28-ESP

面向 ESP-IDF 6.2 的 TG28-ESP 驱动。每类接口的参数及返回值见
[`tg28_esp.h`](include/tg28_esp.h)。

| 接口 | 功能 |
| --- | --- |
| 状态与遥测 | 芯片识别、上下电原因、充电阶段、电流方向、五路 ADC、芯片温度估计、板级 TS 温度曲线与 SOC |
| 电源轨 | DCDC、ALDO、BLDO、CPUSLDO、DC1SW 和 DC4SW 使能，可调轨电压，DCDC PWM、DVM、CCM 与扩频 |
| 充电 | 电芯与备用电源充电、充电设定、输入限制、电池检测、电量计、CHGLED、TS/JEITA 与安全计时 |
| 事件与电源管理 | IRQ 读回、掩码、任务回调与 W1C 应答，PWRON/PWROK、睡眠与唤醒、快速上电、看门狗 |
| 辅助功能 | 四字节暂存区、GPIO1 开漏输出、原始寄存器读取 |

## 上电默认值

`TG28_ESP_CONFIG_DEFAULT()` 只提供 I²C 地址、速率和超时。`tg28_esp_open()` 注册设备
时不读写寄存器，PMU 保留自身 OTP 与各寄存器复位值。应用通过读取 API 获取实际配置。
依据《TG28-ESP Datasheet V1.0》表 6-3，标准出厂 DCDC1 为 3.3 V，DCDC4 为 1.8 V，
RTC-LDO1 为 3.0 V，其余表列输出为关。Candis 台架 REG62 低五位 `0x02` 对应 50 mA。
REG62 和轨设置由各板芯片实际回读确定。

寄存器和精确步进以 TG28-ESP V1.0 手册为准。厂商 Linux 驱动用于对照操作流程和覆盖面。
其中旧版驱动的 200 mA 分支使用 `0x10`，按手册该编码为 1000 mA。组件的
`tg28_esp_set_charge_current(…, 200)` 写入 `0x08`，调用前由应用核对电芯规格、
电流容差与充电电压。

应用负责创建 I²C 总线、选择引脚、串行化对同一 PMU 的访问，以及确定电源轨用途和上下电顺序。
`tg28_esp_open()` 仅向已有总线添加设备。I²C 地址、速度和超时由调用方设置。
更新操作保留寄存器其他位，已达到目标值时跳过写入。请在删除总线前调用
`tg28_esp_close()`。对同一 PMU 的访问需覆盖读改写的整个调用。

```c
#include "tg28_esp.h"

esp_err_t open_power_monitor(tg28_esp_t *pmu, i2c_master_bus_handle_t bus)
{
    const tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    return tg28_esp_open(pmu, bus, &config);
}
```

将 `tg28_esp_t` 初始化为零并保存在应用的设备对象内。打开后调用
`tg28_esp_check_chip_id()` 确认设备，或用 `tg28_esp_read_chip_id()` 获取实际 ID。
使用结束后关闭，失败时保留上下文以重试释放。设备 API 在任务上下文调用。
新 LDO 设置应使用 0–30，依次对应 500–3500 mV、每档 100 mV。Candis 在开启
断电的 LDO 前保存原寄存器值，释放时恢复。`REG90` 控制的 DC1SW 由 DCDC1 供电。
Candis 负责板级负载、供电顺序与复用约束。芯片层的充电、睡眠和硬件保护写操作
应按实际电源设计单独验证。保护设置、软件关机/重启及带复位动作的看门狗
需要在打开设备时显式设置 `allow_protection_changes`。

## 示例与限制

仓库 `examples/tg28` 是独立的只读监测示例。AMOLED 状态监测和受监督的充电
测试见 `examples/power`。详细功能及验证状态见仓库 `docs/tg28-esp-coverage.md`。

在任务中调用 `tg28_esp_dispatch_irq()`，读取事件后选择需要 W1C 的位并调用
`tg28_esp_ack_events()`。共用 IRQ 时由应用同时服务其它设备。调用
`tg28_esp_enter_sleep()` 后，由应用根据板级依赖降低电压或关闭非关键电源轨。
软件唤醒调用需处于 `SLEEP_EN` 已置位状态。

GPADC 采样、TS 模拟数据写入和电量计模型写入仍需芯片厂确认 ESP 料号的适用性。
充电电流为设定值，电池支路电流需要仪表测量。TS 温度曲线须匹配实际 NTC、
偏置和分压网络。固定电阻接法只可回读 TS 电压。

本地或 Git 安装时使用组件目录 `components/tg28-esp`。Registry 发布后可通过
`idf.py add-dependency "NAMESPACE/tg28-esp==VERSION"` 添加独立依赖。

许可证为 [Apache-2.0](LICENSE)。
