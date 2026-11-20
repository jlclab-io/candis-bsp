# Candis Dock 台架示例

使用 ESP-IDF v6.2 的 ESP32-S31 preview 工具链。示例通过组件清单依赖 Dock 和 TG28-ESP，并复用仓库的 USB CDC 与 TF 校验辅助组件。

```sh
idf.py --preview -C examples/dock -B "$PWD/build/dock" \
  -D "SDKCONFIG=$PWD/build/dock/sdkconfig" build
```

通过 Candis 主板的 USB OTG 口下载。首次烧录时，按住 BOOT 复位，确认主机出现 ESP32-S31 ROM 端口后再写入。沿用现有分区布局时先核对应用区。应用启动后通过原生 USB CDC 发送下表命令，一行一条。

| 命令 | 操作 |
| --- | --- |
| `power` | 读取 TG28 的 VBUS、电池、充电设定和电源轨。电池支路的实际电流、电压由台架仪器测量 |
| `dock` | 获取 DC1SW、验证底板 EEPROM 并启用 TF 卡检测 |
| `fixture` | 在已人工核对实物接线的样板上启用 DC1SW 与 TF 卡检测，同时报告底板描述符状态 |
| `id`、`eeprom`、`modules` | 查询底板身份，读取 U10 的 256 字节并报告擦除字节数，或探测 `0x51` 和 `0x52` |
| `tf`、`tf write` | 只读枚举 FAT 目录，或创建独占 4 KiB 文件，执行写入、同步、重开比对、删除。完成后卸载 |
| `screen on`、`screen off` | 启停 AMOLED 和触摸。拖动手指检查标记跟随 |
| `screen panel`、`touch probe` | 面板单独运行，或释放触摸复位后探测 CST9220 的 `0x5A` |
| `screen partial`、`screen full` | 切换局刷与全刷。每 5 秒报告帧率、触摸轮询间隔和错误计数 |
| `tone dock`、`tone l`、`tone r` | 通过描述符选择 CN3 声道，或手动指定 PCM 声道。1 kHz 测试音持续 3 秒并渐入渐出。手动命令后可加 `loud` 或 `100` |
| `echo TEXT` | 核对应用端收到的串口数据 |
| `release` | 释放卡检测、待释放的挂载、屏幕、音频和扩展电源，再用 `power` 查电源轨 |

台架示例需要稳定 USB 供电。默认上电启动 CDC 并读取电源状态，测试命令按需申请 DC1SW、ALDO1 和 ALDO2。待 TF 命令结束并看到卸载结果后再拔卡。测试扬声器前核对 CN3，v1.1 使用左声道，v1.2 使用右声道。保持两个扩展插座空置，或先确认附加模块负载和 GPIO39 状态。

导出示例时一并复制 `examples/common/candis_example_usb` 与 `examples/common/candis_example_tf` 到示例的 `components/`。使用 Registry 包时，应用清单引用同命名空间中的 Dock 与 TG28-ESP，`candis_example_tf` 的清单也改为引用 Dock 所依赖的 Candis 版本。用本地包测试时，可在应用清单用 `override_path` 指向解包后的依赖。
