# Candis 硬件指南

[使用文档](README.md) · [English](../en/hardware.md)

`candis` 组件面向 Candis 硬件修订 1.2。主板采用 ESP32-S31，配备 2.14 英寸 AM214Q480480LK2 AMOLED 模组。引脚定义见 [`candis_board.h`](../../components/candis/candis_board.h)。

## 板载接口

| 接口 | 连接 |
| --- | --- |
| 主 I²C | I²C0，SCL GPIO6，SDA GPIO7，外部上拉 |
| 显示 | ST7801N，480 × 480 RGB565，SPI2 QSPI。CLK GPIO12、CS GPIO10、D0–D3 GPIO11/13/14/9，RESET GPIO2、TE GPIO8 |
| 触摸 | CST9220，主 I²C 地址 `0x5A`，RESET GPIO3，INT GPIO1 |
| 电源管理 | TG28-ESP，主 I²C 地址 `0x34` |
| RTC | RX8130CE，主 I²C 地址 `0x32`，与 PMU 共用 GPIO0 中断 |
| 音频 | ES8389，主 I²C 地址 `0x10`。I²S MCLK/BCLK/WS/DOUT/DIN 为 GPIO15–19，功放使能为 GPIO4 |
| USB-C | 原生 USB OTG，用于 ROM 下载和示例的 CDC 控制台 |

## 供电

- **显示时序。** BSP 默认采用 80 MHz QSPI。应用若需以 50 MHz 运行，可在 `candis_display_config_t` 中设置 `bench_qspi_80mhz = false`。按实际工况验证所用频率。
- **音频。** 使用音频示例前接好麦克风和扬声器，并为 ALDO2 供电。Dock 的 PCM 声道按[扬声器接线](../../components/candis_dock/README.md#hardware)选择。
- **扩展。** TF 卡、底板 EEPROM、BMI270 和已接入的 Dock 模块共用 DC1SW。启用电源管理前，核对整个供电域的负载与时序。插拔底板或模块前断开全部电源。

## 扩展接口

| 资源 | 分配 |
| --- | --- |
| 扩展 I²C | I²C1，SCL GPIO58，SDA GPIO59。探测前释放两脚的 UART0 及外部驱动 |
| 底板 EEPROM | 扩展 I²C 上的 `0x50`，需 DC1SW 上电。当前支持见[扩展硬件](expansion.md#bsp-支持) |
| TF 卡 | 四位 SDMMC，D0–D3 GPIO20–23，CLK GPIO24，CMD GPIO25。Dock 和 Gamepad 的卡检测为 GPIO39，综合测试板为 GPIO35 |
| 运动传感 | BMI270，主 I²C 地址 `0x68`，由扩展域供电 |
| Gamepad SW5 | 与 BMI270 INT1 共用 GPIO38。启用 SW5 前确保 INT1 保持高阻且相关电源轨稳定 |
| OV5640 测试板 | GPIO39/40 用于控制，GPIO46–57 用于 DVP 和 XCLK。按实物选择[摄像头接线配置](../../components/candis-camera/README.md#custom-wiring) |

综合测试板有独立的引脚和供电配置。构建摄像头、存储或输入示例时，选择与实际板卡匹配的配置。BTB 和 Dock 接口的完整针脚见[扩展硬件](expansion.md)。
