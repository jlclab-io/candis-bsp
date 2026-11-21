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

- **显示时序。** BSP 默认采用 80 MHz QSPI。应用若需以 50 MHz 运行，可在 `candis_display_config_t` 中设置 `bench_qspi_80mhz = false`。按实际工况验证所用频率。
- **音频。** 使用音频示例前接好麦克风和扬声器，并为 ALDO2 供电。Dock 的 PCM 声道按[扬声器接线](../../components/candis_dock/README.md#hardware)选择。
- **扩展。** TF 卡、底板 EEPROM、BMI270 和已接入的 Dock 模块共用 DC1SW。启用电源管理前，核对整个供电域的负载与时序。插拔底板或模块前断开全部电源。
- **USB Host。** USB-C 电路控制接口 VBUS 和限流。停止 Host 会释放数据栈与 PHY。使用 Host 前核对供电方式与外设负载。

## 扩展接口

| 资源 | 分配 |
| --- | --- |
| 扩展 I²C | I²C1，SCL GPIO58，SDA GPIO59。探测前释放两脚的 UART0 及外部驱动 |
| 底板 EEPROM | 扩展 I²C 上的 `0x50`，需 DC1SW 上电。当前支持见[扩展硬件](expansion.md#bsp-支持) |
| TF 卡 | 四位 SDMMC，D0–D3 GPIO20–23，CLK GPIO24，CMD GPIO25。Dock 的卡检测为 GPIO39 |
| 运动传感 | BMI270，主 I²C 地址 `0x68`，由 DC1SW 供电。INT1 接 GPIO38，BSP 使用轮询 |
| M1 马达 | GPIO5 驱动高有效低边开关，BLDO2 供电。BSP 脉冲使用 2700 mV，需要确认马达装配和 USB 输入 |
| OV5640 摄像头 | GPIO39/40 用于控制，GPIO46–57 用于 DVP 和 XCLK。按实物选择[摄像头接线配置](../../components/candis-camera/README.md#custom-wiring) |

见[扩展硬件](expansion.md)获取 BTB 和 Dock 连接器引脚定义。
