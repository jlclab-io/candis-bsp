# 示例

[使用文档](README.md) · [English](../en/examples.md)

进入 ESP-IDF 6.2 环境，从仓库根目录执行 `idf.py --preview -C examples/<name> build`。ESP-IDF 6.1 兼容配置使用同一组工程。按 [USB 与固件下载](usb.md)安装示例并打开应用控制台。

## 外设

| 示例 | 演示内容 |
| --- | --- |
| [最小应用](../../examples/candis_minimal) | 标准 I²C 启停、共享引用和释放重试。 |
| [显示](../../examples/display) | ALDO1 上电后体验 LVGL、AMOLED 刷新及触控。 |
| [录音机](../../examples/audio/README.md) | 用触摸屏录放双声道片段。接好麦克风和扬声器，并为 ALDO1/ALDO2 供电。 |
| [音乐播放器](../../examples/music_player/README.md) | 播放内置曲目并选择扬声器声道，需要显示和音频供电。 |
| [摄像头](../../examples/camera) | 按[摄像头指南](../../components/candis-camera/README.md)选择 DVP 型号取帧。AMOLED 可预览 RGB565、YUV422 和灰度画面。 |
| [TF 卡](../../examples/tf_card) | 在 AMOLED 实时显示热插拔状态和根目录。启用 `sdkconfig.write_test` 后，屏幕按钮或控制台 `W` 命令会独占创建 4 KiB 临时文件，写入、同步、回读比较后删除。需为 DC1SW 供电，并按实际板型配置卡检测脚。 |
| [扩展板](../../examples/expansion) | 识别 Dock 后浏览 TF 卡，可选探测模块地址。 |
| [Dock](../../examples/dock/README.md) | 识别 Dock 并测试 TF 卡、BMI270、显示和扬声器。 |
| [ADC](../../examples/adc) | 采集两路模拟信号，默认使用 Dock LEFT 扩展座。 |
| [运动传感](../../examples/motion) | 在扩展域已上电时读取 BMI270 运动数据和温度。 |
| [RTC](../../examples/rtc) | 读取或设置 UTC，并体验 RTC 更新唤醒。见 [RTC 控制台](#rtc-控制台)。 |
| [电源](../../examples/power/README.md) | 在 AMOLED 上显示电源与充电遥测，充电条件检查通过后设置默认 200 mA 档位。 |
| [TG28 监测](../../examples/tg28/README.md) | 独立应用通过 I²C 读取 TG28-ESP 状态。 |
| [马达](../../examples/motor/README.md) | 为 Candis Dock 已安装的 M1 马达输出短时 PWM 脉冲。 |

## 板级与系统

| 示例 | 演示内容 |
| --- | --- |
| [底板识别](../../examples/baseboard) | 读取受支持的底板描述符，运行前为 DC1SW 供电并释放 UART0。 |
| [GPIO](../../examples/gpio) | 切换板载 LED，确保扩展硬件留出 GPIO37。 |
| [Wi-Fi 扫描](../../examples/wifi_scan) | 发现附近的无线接入点。 |
| [BLE 扫描](../../examples/ble_scan) | 发现附近的蓝牙 LE 广播。 |
| [Wi-Fi 与 BLE 共存](../../examples/bleprph_wifi_coex/README.md) | 在 BLE 广播期间扫描 Wi-Fi，连接无线网络并验证 HTTP 访问，同时接收 GATT 写入。 |
| [睡眠](../../examples/sleep) | 进入由定时器唤醒的浅睡眠或深睡眠，唤醒后重新连接控制台。 |
| [Flash 信息](../../examples/flash) | 读取 Flash 标识、容量和运行镜像哈希。 |

## 板型配置

| 示例 | 硬件 | GPIO 说明 |
| --- | --- | --- |
| TF 卡 | Candis Dock | Dock 卡检测使用 GPIO39 |
| ADC | Candis Dock（LEFT 扩展座） | GPIO42 和 GPIO43 分别在 U6.20 和 U6.18 |
| 摄像头 | Dock v1.2 U7 或应用自有 DVP 装配 | 使用 U7 时由模组或转接板处理 RESET 和 PWDN。匹配装配供电并释放 UART0 供 SCCB 使用。见[摄像头接线配置](../../components/candis-camera/README.md#application-wiring) |
| LED 和马达 | 核心板和 Dock | LED 用 GPIO37，M1 马达用 GPIO5 |

使用 Dock TF 卡时，保持 LEFT U6 空置，或使其 GPIO39 线路保持高阻。使用 FAT 格式卡。使用核心板 LED 前，确保 U6.8 上的 GPIO37 可用。外接 ADC 信号须与板卡共地，并符合 0 dB 衰减档位的输入范围。

TF 示例在 ESP-IDF 6.2 使用 VFS 只读浏览挂载，在 ESP-IDF 6.1 由示例将浏览操作限制为目录读取。

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
