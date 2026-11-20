# Candis 示例

在仓库根目录运行 `idf.py --preview -C examples/<目录> build`。默认控制台是 Candis 板载 USB-C 的原生 OTG CDC，综合测试板的 USB Serial/JTAG 可用于初次烧录和恢复。已运行 BSP 示例的板子可以用仓库的应用区更新工具切换示例，见 [USB 调试](../docs/usb.md)。

| 工程 | 运行方式和结果 | Bring-Up 对应 |
| --- | --- | --- |
| [tf_card](tf_card) | SDMMC 4-bit，插卡后只读列目录，按 `L` 重试挂载，`Q` 停止并释放 DC1SW。可选 `W` 校验 4 KiB 文件 | 21 TF 卡 |
| [adc](adc) | 两路 ADC 单次转换，每路 16 点平均和极值，打印校准毫伏，按 `S` 再采一轮 | 16 GPIO/ADC、20 摇杆 |
| [gpio](gpio) | GPIO37 开漏驱动低有效板载 LED，并回读高低电平。使用主板或综合测试板，正式底板上的 GPIO37 扩展口须空闲 | 01 电源启动、08 BTB GPIO |
| [wifi_scan](wifi_scan) | 被动扫描附近 AP，输出 SSID、BSSID、信道、RSSI 和认证模式。按 `S` 重扫 | 15 Wi-Fi/BLE RF |
| [ble_scan](ble_scan) | 连续运行 5 秒被动扫描窗口。可在 `menuconfig` 设置对端的 128 位 Service Data UUID，单独统计该对端的广播报告和最强 RSSI | 15 Wi-Fi/BLE RF |
| [sleep](sleep) | `L` 轻睡、`D` 深睡，均由 4 秒定时器唤醒。重连 CDC 后按 `?` 读取结果 | 10 ESP 睡眠唤醒 |
| [baseboard](baseboard) | 读取 0x50 的 EEPROM 描述符和 CRC，按 `P` 重读。要求 DC1SW 已上电，并释放 GPIO58/59 上的 UART0 和回环夹具 | 26 底板 EEPROM |
| [flash](flash) | 只读查询 JEDEC ID、Flash 实际容量和运行中镜像的 SHA-256，按 `I` 重查 | 05 Flash 完整性 |
| [display](display) | AMOLED 区域刷新、TE、扫描条、CST9220 触控轨迹、渲染统计 | 02 AMOLED 触控 |
| [audio](audio/README.md) | AMOLED 触控录音、停止、回放和双通道峰值 | 06 I²S 音频 |
| [music_player](music_player/README.md) | 内置曲目循环播放、实时音量、左右扬声器选择和双通道峰值 | 06 I²S 音频 |
| [camera](camera) | OV5640 实时预览、单帧、可选 TF 卡 JPEG 和调参接口 | 22 DVP 摄像头 |
| [rtc](rtc) | UTC 读取和设置、RTC 更新中断深睡唤醒 | 04 PMU I²C/RTC |
| [motion](motion) | BMI270 六轴、温度及与 TF 共用 DC1SW | 20 IMU |
| [motor](motor/README.md) | 综合测试板 M1 PWM 控制和 BLDO2 恢复 | 23 马达 |
| [power](power/README.md) | 充电状态、VBAT、电源轨及可选受监督充电档位验证 | 01 电源、18 电池充电 |
| [tg28](tg28/README.md) | 独立使用 TG28-ESP 组件读取 PMU 和充电遥测 | 04 PMU I²C、18 电池充电 |
| [expansion](expansion) | 按底板身份运行 Dock TF 和模块探测，或 Gamepad 摇杆、按键和编码器 | 20 摇杆、21 TF、26 底板 EEPROM |
| [diagnostics](diagnostics) | 板级检查，另有音频、TF、USB CDC 回显配置 | 09 系统诊断、13 USB OTG CDC |

## TF 卡

默认配置面向综合测试板的 GPIO35 CD，并使用已供电的 DC1SW。正式 Dock 或 Gamepad 在 `menuconfig` 选择 GPIO39 CD。插入 FAT 格式卡后，示例去抖、挂载、最多显示 16 项并卸载。综合测试板可叠加两个配置，启用 DC1SW 管理和手动写读。

```sh
idf.py --preview -C examples/tf_card -B "$PWD/build/tf-fixture" \
  -D "SDKCONFIG=$PWD/build/tf-fixture/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.fixture;sdkconfig.write_test' build
```

`W` 独占创建一个未使用的 `CDSxxxx.TST`，写入并同步，重新打开比较 4096 字节，最后删除。等待 `TF_RESULT` 后再拔卡。由示例管理电源时，先发 `Q` 并等待 `TF_RELEASE` 再切换固件。

## ADC

综合测试板摇杆接 GPIO43/44，由 DC1SW 供电。正式 Gamepad 摇杆接 GPIO42/43，由 ALDO3 供电，使用 `sdkconfig.gamepad`，并预先满足该底板的供电条件。接外部模拟量时共地，按 ESP32-S31 的 0 dB 输入范围接线。输出的 `voltage=unavailable` 表示未取得校准值，`raw` 仍可用于观察相对变化。

综合测试板可启用自动接管 DC1SW。每轮持续约 6 秒，结束后恢复电源状态。按 `S` 可重复采样。

```sh
idf.py --preview -C examples/adc -B "$PWD/build/adc-fixture" \
  -D "SDKCONFIG=$PWD/build/adc-fixture/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.fixture' build
```

## 测试边界

无线示例验证发现和接收，RF 定量测量沿用 Bring-Up 的夹具和对端。GPIO 示例回读板载 LED 引脚，BTB 外部通断需用夹具。USB Host/Type-C 角色切换、USB-JTAG 恢复、LP 核 I²C、Flash 擦写和整机生产回归仍使用独立的 Bring-Up 工程。
