# 示例

[使用文档](README.md) · [English](../en/examples.md)

进入 ESP-IDF 6.2 环境，从仓库根目录执行 `idf.py --preview -C examples/<name> build`。按 [USB 与固件下载](usb.md)安装示例并打开应用控制台。

## 外设

| 示例 | 演示内容 |
| --- | --- |
| [显示](../../examples/display) | ALDO1 上电后体验 LVGL、AMOLED 刷新及触控。 |
| [录音机](../../examples/audio/README.md) | 用触摸屏录放双声道片段。接好麦克风和扬声器，并为 ALDO1/ALDO2 供电。 |
| [音乐播放器](../../examples/music_player/README.md) | 播放内置曲目并选择扬声器声道，需要显示和音频供电。 |
| [摄像头](../../examples/camera) | 在 AMOLED 上预览 OV5640。使用摄像头测试板配置，拍照和 JPEG 选项见[摄像头指南](../../components/candis-camera/README.md)。 |
| [TF 卡](../../examples/tf_card) | 浏览 FAT 格式卡。按实际板型配置卡检测脚，并为 DC1SW 供电。 |
| [扩展板](../../examples/expansion) | 识别底板后读取 Gamepad 输入或浏览 Dock 存储。 |
| [Dock](../../examples/dock/README.md) | 开启 Dock 会话、浏览 TF 卡，并使用显示或扬声器。 |
| [ADC](../../examples/adc) | 采集两路摇杆轴，可用时输出标定毫伏值。按实物选择综合测试板或 Gamepad 配置。 |
| [运动传感](../../examples/motion) | 在扩展域已上电时读取 BMI270 运动数据和温度。 |
| [RTC](../../examples/rtc) | 读取或设置 UTC，并体验 RTC 更新唤醒。见 [RTC 控制台](#rtc-控制台)。 |
| [电源](../../examples/power/README.md) | 在 AMOLED 上显示电源与充电遥测，默认配置只读 PMU。 |
| [TG28 监测](../../examples/tg28/README.md) | 独立应用通过 I²C 读取 TG28-ESP 状态。 |
| [马达](../../examples/motor/README.md) | 为综合测试板已安装的马达输出短时 PWM 脉冲。 |

## 板级与系统

| 示例 | 演示内容 |
| --- | --- |
| [底板识别](../../examples/baseboard) | 读取受支持的底板描述符，运行前为 DC1SW 供电并释放 UART0。 |
| [GPIO](../../examples/gpio) | 切换板载 LED，确保扩展硬件留出 GPIO37。 |
| [Wi-Fi 扫描](../../examples/wifi_scan) | 发现附近的无线接入点。 |
| [BLE 扫描](../../examples/ble_scan) | 发现附近的蓝牙 LE 广播。 |
| [睡眠](../../examples/sleep) | 进入由定时器唤醒的浅睡眠或深睡眠，唤醒后重新连接控制台。 |
| [Flash 信息](../../examples/flash) | 读取 Flash 标识、容量和运行镜像哈希。 |
| [板级检查](../../examples/diagnostics) | 检查核心板外设，features 配置还可启用音频、TF 卡和 CDC 回显。 |

## 板型配置

为不同变体使用独立的构建目录和 `SDKCONFIG` 文件。例如 Gamepad 的模拟摇杆在 GPIO42/43，需要 ALDO3 供电，可用下列命令构建。综合测试板的 GPIO43/44 摇杆由 DC1SW 供电，使用 `sdkconfig.fixture`。接入外部模拟信号时，按 ADC 输入范围接线并确保共地。

```sh
idf.py --preview -C examples/adc -B "$PWD/build/adc-gamepad" \
  -D "SDKCONFIG=$PWD/build/adc-gamepad/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.gamepad' build
```

使用 TF 卡示例时，Dock 和 Gamepad 选择 GPIO39，综合测试板选择 GPIO35。Dock U6 保持空置，或确认其 GPIO39 保持高阻。浏览目录时使用 FAT 格式卡。

## RTC 控制台

[RTC 示例](../../examples/rtc)启动时读取日历。以下控制台命令以换行结束。

| 命令 | 操作 |
| --- | --- |
| `READ` | 读取 UTC 日历 |
| `SET <epoch>` | 将有效日历设为指定的 UTC Unix 时间戳 |
| `RECOVER <epoch>` | 恢复掉电后失效的日历 |
| `SLEEP` | 进入带 RTC 更新唤醒及定时器兜底的深睡眠 |
| `WAKE` | 重新连接串口后查询上次唤醒 |

在类 Unix 主机上用 `date -u +%s` 获取 UTC Unix 时间戳。深睡前先确认日历有效。唤醒后 USB 串口可能重新枚举。RTC 后备保持与睡眠电流取决于硬件配置。
