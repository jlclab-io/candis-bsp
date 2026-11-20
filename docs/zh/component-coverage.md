# 组件支持

[使用文档](README.md) · [English](../en/component-coverage.md)

| 组件 | 可用功能 | 硬件要求与使用范围 |
| --- | --- | --- |
| `candis` | AMOLED、触摸、RTC、电源轨状态、TF 挂载及可选 I²S 音频和 BMI270 | Candis rev. 1.2。音频、TF 和运动传感需要相应器件与供电。见[硬件指南](hardware.md)。 |
| `tg28-esp` | 遥测、稳压器、充电配置、中断与电源时序 | 可配置的 I²C 驱动。芯片及板级要求见[驱动指南](tg28-esp-coverage.md)。 |
| `candis_dock` | 底板识别、TF、卡检测和 PCM 声道选择 | 带受支持描述符的 Dock rev. 1.1 或 1.2。U6 必须给卡检测留出 GPIO39。子模块探测提供地址响应。 |
| `candis_gamepad` | 摇杆 ADC、可选毫伏换算、按键去抖及正交解码 | Gamepad rev. 1.0。使用组装后的摇杆标定。启用 SW5 时保持共线的 BMI270 中断为高阻。 |
| `candis-camera` | OV5640 DVP 采集、拍照、连续取流和传感器控制 | 使用经过核对的 OV5640 接线和供电配置。AMOLED 示例默认采用 RGB565。其他格式和传感器参数按实际装配验证。 |

CI 覆盖示例及组件包的构建。使用目标硬件验收应用，尤其是共享电源轨与 GPIO、充电、RTC 后备及睡眠行为和显示时序。EEPROM 规范在现有 BSP 中的支持范围见[扩展指南](expansion.md#bsp-支持)。
