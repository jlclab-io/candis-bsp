# Candis Dock

Candis 的可选扩展底板组件。通过主 BSP 的公共接口管理 DC1SW 引用、读取底板描述符，并使用 Candis 的 SDMMC 和 FAT VFS 驱动。应用在一块 Candis 上使用一个 Dock，由控制任务管理会话。

## 硬件连接

| 功能 | 连接 |
| --- | --- |
| TF 卡 | D0–D3 为 GPIO20–23，CLK 为 GPIO24，CMD 为 GPIO25。GPIO39 为低有效卡检测，同时接到 U6.10 |
| 扩展 I²C | SCL 为 GPIO58，SDA 为 GPIO59。U10 AT24C02 通过 Q3 连接，7 位地址为 `0x50`，地址脚和 WP 接地。U6/U7 共用总线 |
| DC1SW | 同时供 TF、U10、BMI270 和两个扩展座的 DC1SW 引脚。按 TG28-ESP 的 300 mA 域负载规格核算 |
| CN3 扬声器 | v1.1 为 LSPK+/−，v1.2 为 RSPK+/−，均为 Candis 主板功放的差分输出。按实物选 PCM 声道 |
| CN4 电池 | VBAT 与 GND。R12 固定 10 kΩ 接 TG28 TS。充电策略和电气保护由主板及电池系统管理 |
| USB | 下载及 CDC 使用 Candis 主板的 USB OTG 接口 |

使用扩展 I²C 前释放 UART0 和 GPIO58/59 的外部驱动。使用 TF 前确认 U6 的 GPIO39 高阻或插座空置。模块地址 `0x51`、`0x52` 是接口约定，ACK 用于诊断总线响应。模块识别及引脚方向以所接模块的资料和描述符为准。

v1.1 与 v1.2 使用相同的 TF 和 EEPROM 连线。Candis 校验 v1.1（`0x0101`）和 v1.2（`0x0102`）的 Dock 描述符，公共枚举沿用 `CANDIS_BASEBOARD_BB`。通过 `candis_dock_get_speaker_channel()` 选择 CN3 对应的 PCM 声道。未编程的样板可用独立示例的手动台架模式检查外设。

## 接入

在应用主组件的 `idf_component.yml` 中添加到 `components/candis_dock` 的本地路径依赖，Candis 依赖由组件清单解析。

1. 电源与总线条件确认后，调用 `candis_dock_open(manage_power, &identity)` 验证 U10 并保持会话的 DC1SW 引用。使用已有电源轨时传 `false`。应用主动启用 DC1SW 时传 `true`。已有独立供电所有者时，可用 `candis_dock_probe()` 读取身份。
2. 使用 `candis_dock_detect_start()` 和 `candis_storage_detect_read()` 跟踪卡状态，由应用消抖。用 `candis_dock_mount_with_config()` 设置只读策略。挂载失败时保留卡上数据。
3. 关闭文件和目录句柄，依次调用 `candis_storage_unmount()`、`candis_storage_detect_stop()`、`candis_dock_close()`。释放失败时按同一顺序重试。各 Candis 外设仍持有的电源引用会继续生效。

通过 `candis_dock_open()` 控制 DC1SW 时，由 Dock 使用 BSP 的应用电源引用。其他 Candis 驱动可以继续使用自身的电源引用。应用模型详见 [Dock 独立示例](../../examples/dock/README.md)。通用扩展示例在 [examples/expansion](../../examples/expansion)。

仓库 CI 用 `tools/stage_registry_extension.py` 将 Candis 的本地路径依赖转换为同命名空间的 Registry 版本依赖，并从解包组件构建消费者。发布时先准备可解析的 Candis 版本，再使用仓库配置的 Registry 命名空间。
