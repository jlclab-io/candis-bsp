# Candis BSP

[English](README.md) · **简体中文**

[![ESP-IDF 构建](https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml)

[快速开始](#快速开始) · [组件](#组件) · [示例](#示例) · [使用文档](docs/zh/README.md)

**Candis** 是一款 ESP32-S31 开发板，配备 2.14 英寸、480 × 480 AMOLED 触摸屏。BSP 提供显示、触摸、RTC 和板级电源接口，可按硬件配置启用音频、运动传感和存储。项目目前处于开发预览阶段。

## 支持的硬件

| 硬件 | 修订 | 支持功能 |
| --- | --- | --- |
| Candis | 1.2 | 可独立使用的核心板，配备 AMOLED、电容触控、RTC 和原生 USB-C |
| Candis Dock | 1.1、1.2 | 可选底板，提供 TF（microSD）、扬声器接口及两个扩展座 |
| Candis Gamepad | 1.0 | 可选底板，提供摇杆轴、按键和旋转编码器 |

断电后安装底板。每次只装一块，并同时连接两组板对板接口。供电与引脚要求见[硬件指南](docs/zh/hardware.md)。

## 快速开始

1. 使用 ESP-IDF 6.2 和 ESP32-S31 工具链。在 ESP-IDF 目录运行 `./install.sh esp32s31` 和 `source ./export.sh`。[CI 验证的 ESP-IDF 修订](https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb)需要用 `--preview` 启用 ESP32-S31。
2. 构建显示示例。

   ```sh
   git clone https://github.com/jlclab-io/candis-bsp.git
   cd candis-bsp
   idf.py --preview -C examples/display build
   ```

3. 使用支持数据传输的 USB 线连接板卡。确认 AMOLED 已供电，按 [USB 与固件下载](docs/zh/usb.md)烧录示例。进入 **Partial**、**Full** 或 **Scan** 页面，并拖动手指体验触控。

### 在应用中使用 Candis

在应用组件的 `idf_component.yml`（例如 `main/idf_component.yml`）中添加依赖。将 `<commit-or-tag>` 替换为 Candis BSP 的 Git 提交或标签。

```yaml
dependencies:
  candis:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis
    version: "<commit-or-tag>"
```

本地开发时可使用相对于清单的 `path`。例如应用与 BSP 在同级目录时，该路径为 `../../candis-bsp/components/candis`。包含 `bsp/esp-bsp.h`，调用 `bsp_display_start()` 启动 LVGL。配置与示例见 [API 指南](docs/zh/api.md#显示与触摸)。

## 组件

| 组件 | 用途 |
| --- | --- |
| [`candis`](components/candis/README.md) | 核心板 BSP 和共享外设资源 |
| [`tg28-esp`](components/tg28-esp/README.md) | 可独立使用的 TG28-ESP 电源管理驱动 |
| [`candis-camera`](components/candis-camera/README.md) | 可配置的 OV5640 DVP 采集 |
| [`candis_dock`](components/candis_dock/README.md) | Dock 识别、TF 卡和扬声器声道选择 |
| [`candis_gamepad`](components/candis_gamepad/README.md) | Gamepad 摇杆轴、按键和编码器 |

## 示例

| 示例 | 演示内容 |
| --- | --- |
| [显示](examples/display) | AMOLED 上的 LVGL 界面和触控 |
| [录音机](examples/audio/README.md) · [音乐播放器](examples/music_player/README.md) | 双声道录音和播放 |
| [摄像头](examples/camera) | OV5640 预览和拍照 |
| [Dock](examples/dock/README.md) · [扩展板](examples/expansion) | Dock 存储和 Gamepad 输入 |

更多集成说明见[完整示例目录](docs/zh/examples.md)、[硬件指南](docs/zh/hardware.md)和 [EEPROM V1 规范](docs/zh/eeprom-v1.md)。

## 许可证

[Apache-2.0](LICENSE)。音乐播放器示例内置曲谱采用 [CC0-1.0](examples/music_player/README.md)。
