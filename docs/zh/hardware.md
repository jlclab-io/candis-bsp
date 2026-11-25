# Candis 硬件指南

[使用文档](README.md) · [English](../en/hardware.md)

`candis` 组件面向 Candis 硬件修订 1.2。主板采用 ESP32-S31，配备 2.14 英寸 AM214Q480480LK2 AMOLED 模组。引脚定义见 [`candis_board.h`](../../components/candis/priv_include/candis_board.h)。

## 板载接口

| 接口 | 连接 |
| --- | --- |
| 主 I²C | I²C0，SCL GPIO6，SDA GPIO7，外部上拉 |
| 显示 | ST7801N，480 × 480 RGB565，SPI2 QSPI。CLK GPIO12、CS GPIO10、D0–D3 GPIO11/13/14/9，RESET GPIO2、TE GPIO8 |
| 触摸 | CST9220，主 I²C 地址 `0x5A`，RESET GPIO3，INT GPIO1 |
| 电源管理 | TG28-ESP，主 I²C 地址 `0x34` |
| RTC | RX8130CE，主 I²C 地址 `0x32`，与 PMU 共用 GPIO0 中断 |
| 音频 | ES8389，主 I²C 地址 `0x10`。I²S MCLK/BCLK/WS/DOUT/DIN 为 GPIO15–19，功放使能为 GPIO4 |
| USB-C | 原生 USB OTG，支持 ROM 下载、CDC Device 和 USB Host |

## 供电

- **显示时序。** BSP 默认采用 80 MHz QSPI，也可通过 `candis_display_config_t` 配置为 50 MHz。
- **音频。** 使用音频示例前接好麦克风和扬声器，并为 ALDO2 供电。Dock 的 PCM 声道按[扬声器接线](../../components/candis-dock/README.md#hardware)选择。
- **扩展。** BTB 引出 TG28 电源轨、GPIO、I²C、USB 相关引脚、音频信号和电源。核心 BSP 不预设底板器件或 GPIO。当前 Dock 上，TF 卡、底板 EEPROM、BMI270 和 U6/U7 扩展座共用 DC1SW；启用电源管理前须把整个供电域的负载与时序纳入配置。
- **USB Host。** USB-C 电路控制接口 VBUS 和限流。停止 Host 会释放数据栈与 PHY。使用 Host 前核对供电方式与外设负载。

## 扩展接口

`candis` 组件只固定上面的核心板资源。底板器件型号、地址、存储引脚、马达 PWM 和可选 I²C 控制器由应用或独立底板组件提供。

| 装配 | 配置归属 |
| --- | --- |
| 核心 BSP | 不预设底板 SDMMC、卡检测、IMU 地址、马达 GPIO 或扩展 I²C 控制器 |
| Candis Dock v1.1/1.2 | `candis-dock` 固定 GPIO58/59 上的 I²C1、描述符 `0x50`、GPIO20–25 的 TF 数据与 GPIO39 卡检测、Dock 扬声器路由和硬件修订规则 |
| 应用自有外设 | 存储、运动传感、马达和摄像头示例展示如何传入应用自有引脚与器件，实际使用时应按装配修改配置。见 [TF 卡](../../examples/tf_card)、[运动传感](../../examples/motion)、[马达](../../examples/motor) 和 [摄像头](../../examples/camera) |
| Dock 摄像头接口 | 当前 Dock 用 GPIO46–57 提供 8 位 DVP，GPIO58/59 提供 SCCB，并引出 PMU 控制的 BLDO1、DC1SW、ALDO3、ALDO4。摄像头示例通过资源适配器传入这组接线。见 [U7 接口引脚表](expansion.md#dock-20pin) |

见[扩展硬件](expansion.md)获取 BTB 和 Dock 连接器引脚定义。
