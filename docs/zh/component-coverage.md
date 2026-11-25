# 组件支持

[使用文档](README.md) · [English](../en/component-coverage.md)

| 组件 | 可用功能 | 硬件要求与使用范围 |
| --- | --- | --- |
| `candis` | ESP-BSP 通用接口、AMOLED、触摸、RTC、电源，可选 LVGL、音频、BMI270 和 USB Device/Host | Candis 核心板。TF 需要识别到 Dock。BMI270 和马达需要实际装配和供电。见[硬件指南](hardware.md)。 |
| `tg28-esp` | 遥测、稳压器、充电配置、中断与电源时序 | 可配置的 I²C 驱动。芯片及板级要求见[驱动指南](tg28-esp-coverage.md)。 |
| `candis-dock` | 底板识别、TF、卡检测和 PCM 声道选择 | 带受支持描述符的 Dock rev. 1.1 或 1.2。GPIO39 与 U6.10 共用，使用卡检测时 U6 侧须保持高阻。U6/U7 探测只报告预留地址 ACK。 |
| `candis-camera` | OV5640 DVP 采集、拍照、连续取流和传感器控制 | 使用经过核对的 OV5640 接线和供电配置。AMOLED 示例默认采用 RGB565。其他格式和传感器参数按实际装配验证。 |

共享电源轨、GPIO、充电、RTC 后备和睡眠行为按目标装配配置。Dock EEPROM 字段与当前接受的身份见[Dock EEPROM](eeprom-v1.md)。
