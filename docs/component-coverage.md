# Candis BSP 功能覆盖与验收

已往实板日志见 [硬件记录](hardware.md)。表中的构建和主机测试用于核对当前代码，
新功能的硬件验收需记录运行镜像、板型和实测结果。

| 组件或外设 | 软件支持与入口 | 仍需实板或上游证据 |
| --- | --- | --- |
| Candis AMOLED / CST9220 | `examples/display`，TE、QSPI DMA、LVGL 异步刷新、区域更新、触控轨迹及资源使用统计。相机使用独占面板模式 | 本版光学延迟、热稳态和长时间负载。80 MHz 裕量及 ALDO1 与 IOVCC 容差 |
| Candis ES8389 / I²S | 48 kHz 双通道，增益回读、PA 控制、静音、峰值限制。`examples/audio` 提供 AMOLED 触控录放、实时左右声道峰值和削波提示，单段最长 3 秒 | 新版触控启停、左右声道归属、重复录放后的 ALDO2 恢复、POP、响度和声学指标 |
| Candis RTC / USB / TF / IMU | RTC UTC 校时和更新中断、OTG CDC 诊断及下载握手、四位 SDMMC、DC1SW 共享引用、BMI270 六轴与传感器时间突发读取及芯片温度读取 | RTC 后备保持、USB 电气与冷启动、BMI270 本版读取时延、正式扩展板 TF/IMU、电流与故障顺序 |
| `tg28-esp` | 独立 I²C PMU、轨配置、充电、ADC/电量、IRQ、睡眠和看门狗。[逐项覆盖](tg28-esp-coverage.md) | 电芯支路测量、掉轨/唤醒及保护；GPADC、TS 写入和电量模型需厂商澄清 |
| `candis_dock` | BTB 识别复用 Candis 描述符；GPIO39 TF 检测、共享 DC1SW、只读挂载。`examples/expansion` 去抖插拔、列出 FAT 根目录，可独立启用 4 KiB 文件校验及地址 `0x51`/`0x52` ACK 检查 | 正式 Dock EEPROM、TF 插拔和读写、U6/U7 模块图纸和地址脚、信号方向、供电约束及并用验证 |
| `candis_gamepad` | GPIO42/43 ADC 支持按通道转换为毫伏。摇杆与编码器按键、SW3/4/6、可选 SW5，独立按键去抖与 A/B 正交解码。`examples/expansion` 串口及 AMOLED 实时读数 | 正式板摇杆电压、中心/行程/方向、编码器最高可靠转速、按键极性和 GPIO38 共线波形 |
| `candis-camera` | ESP Video DVP 与模式选择、OV5640 芯片 ID、翻转、测试图、AE target、曝光/增益/AWB、SDE/CMX 效果、CIP 锐化/降噪和 16 点 gamma。示例支持 480×480 预览、单帧展示和 TF 卡 JPEG 保存 | 成像控制响应、曝光到屏显延迟、gamma 和 JPEG 色彩、实卡读写和长稳 |

## OV5640 上游范围

ESP Cam Sensor 2.6.0 的 OV5640 DVP 表列 800×600 RGB565 大小端与
YUV422 UYVY/YUYV。当前台架验收记录适用于直接 RGB565 预览。模式在构建时选择。
`sdkconfig.yuyv` 将 YUYV 交给 ESP Video 的 DVP 硬件转换，预览和 JPEG 配置均已构建，
实际色彩、字节顺序及帧率仍待台架验证。
`sdkconfig.uyvy` 采集原生 UYVY，按 BT.601 软件转换，可搭配串口调参或 JPEG。
`ov5640_set_format()` 显式格式分支可能把空复位表交给 `ov5640_write_array()`，
因此运行时切换受限。Candis 通过 ESP Cam Sensor 的寄存器通道实现 OV5640 曝光、增益
和白平衡。亮度、对比度和饱和度按 OV5640 v2.03 §7.26 的 SDE 位域配置。
分组时序、AE/AWB 模式、CIP 位域、CMX saturation 表、特效表、gamma 曲线、参数边界和传输失败清理通过主机模拟。传感器原生 JPEG、自动对焦、色相、DPC、镜头校正和防闪烁仍待适配和实板验证。
JPEG 保存示例使用 ESP32-S31 硬件编码 RGB565 帧。
[ESP Cam Sensor 的 OV5640 源码](https://github.com/espressif/esp-video-components/blob/master/esp_cam_sensor/sensors/ov5640/ov5640.c) ·
[ESP32 Camera 的 OV5640 源码](https://github.com/espressif/esp32-camera/blob/master/sensors/ov5640.c)

## 正式底板接口边界

Gamepad 与 Dock 各自使用完整 CN1/CN2 接口。Dock U6/U7 共享扩展 I²C，部分信号
与 TF/LED 共网。固件只识别已验证格式的 EEPROM 描述符，按已证实的接口使用 GPIO。
Gamepad GPIO38 共用 SW5 和 BMI270 INT1，软件采样需 INT1 保持高阻，台架还需验证
DC1SW、ALDO3 顺序及断电时的反灌。扩展枚举由应用显式调用。
