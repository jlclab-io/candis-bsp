<div align="center">
  <h1>Candis BSP</h1>
  <p><strong>Candis 开发板的 ESP-IDF 支持包</strong></p>
  <p>ESP32-S31 &nbsp;·&nbsp; 2.14 英寸 480 × 480 AMOLED 触摸屏 &nbsp;·&nbsp; Candis Dock 扩展</p>
  <p>
    <a href="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml"><img alt="ESP-IDF 构建" src="https://github.com/jlclab-io/candis-bsp/actions/workflows/ci.yml/badge.svg?branch=main"></a>
    <a href="components/candis/idf_component.yml"><img alt="目标芯片 ESP32-S31" src="https://img.shields.io/badge/目标芯片-ESP32--S31-00979D"></a>
    <a href="https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb"><img alt="ESP-IDF 6.2.x" src="https://img.shields.io/badge/ESP--IDF-6.2.x-E7352C"></a>
    <a href="LICENSE"><img alt="许可证 Apache-2.0" src="https://img.shields.io/badge/许可证-Apache--2.0-blue"></a>
  </p>
  <p>
    <a href="#快速开始"><strong>快速开始</strong></a> ·
    <a href="#板卡">板卡</a> ·
    <a href="docs/zh/examples.md">示例</a> ·
    <a href="docs/zh/README.md">使用文档</a> ·
    <a href="README.md">English</a>
  </p>
</div>

## 快速开始

**准备 ESP-IDF。** 使用 [ESP-IDF 6.2](https://github.com/espressif/esp-idf/commit/2b10d1278a547f5dc92efbe516144b4ae85f80cb)。在 ESP-IDF 目录运行 `./install.sh esp32s31` 和 `source ./export.sh`。

**构建显示示例。** 使用此 SDK 修订时，ESP32-S31 需要 `--preview`。

```sh
git clone https://github.com/jlclab-io/candis-bsp.git
cd candis-bsp
idf.py --preview -C examples/display build
```

**烧录并体验触控。** 用支持数据传输的 USB-C 线连接板卡，确认 AMOLED 已供电，按[烧录指南](docs/zh/usb.md)烧录。进入 **Partial**、**Full** 或 **Scan** 页面，拖动手指体验触控。

> [!TIP]
> 如果烧录失败，长按 **PWRON** **6 秒**关机。然后按住 **BOOT**，同时按下 **PWRON** **2 秒**开机，进入 ROM 下载模式。选择 ROM 串口并重新烧录。

<details>
<summary><strong>在自己的应用中接入 Candis</strong></summary>

在应用组件的 `idf_component.yml`（例如 `main/idf_component.yml`）中添加以下依赖，将 `<commit-or-tag>` 替换为 BSP 提交或标签。

```yaml
dependencies:
  candis:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis
    version: "<commit-or-tag>"
```

当应用与 BSP 在同级目录时，可用相对于清单的 `path: ../../candis-bsp/components/candis`。包含 `bsp/esp-bsp.h`，调用 `bsp_display_start()` 启动 LVGL，再调用 `bsp_display_backlight_on()` 点亮。见 [API 指南](docs/zh/api.md#显示与触摸)。

</details>

## 板卡

| 板卡 | 修订 | 接口 |
| --- | --- | --- |
| **Candis** | 1.2 | 核心板，配备 AMOLED、触控、RTC 和 USB-C |
| **Candis Dock** | 1.1、1.2 | TF（microSD）、扬声器接口和两个扩展座 |

断电后安装底板。每次装一块，并同时接通两组板对板接口。[硬件与供电](docs/zh/hardware.md) · [扩展与 EEPROM](docs/zh/expansion.md)

## 示例

- [**最小应用**](examples/candis_minimal)演示 I²C 首次初始化、共享引用、释放失败和恢复。
- [**显示**](examples/display)启动 AMOLED 与 LVGL 触控界面。
- [**录音机**](examples/audio/README.md)和[**音乐播放器**](examples/music_player/README.md)演示双声道录音和播放。
- [**摄像头**](examples/camera)使用 OV5640 模块预览和采集图像。
- [**Dock**](examples/dock/README.md)和[**扩展板**](examples/expansion)演示 TF 卡访问和模块探测。

[查看全部示例 →](docs/zh/examples.md)

## 组件

- [`candis`](components/candis/README.md) 提供核心板的显示、触控、RTC、电源和外设接口。
- [`tg28-esp`](components/tg28-esp/README.md) 是独立的 TG28-ESP 电源管理驱动。
- [`candis-camera`](components/candis-camera/README.md) 提供可配置的 OV5640 DVP 采集。
- [`candis-dock`](components/candis-dock/README.md) 提供 Dock 识别、TF 卡访问与扬声器声道选择。

BSP 处于开发预览阶段。硬件支持范围见[组件支持](docs/zh/component-coverage.md)。

## 许可证

[Apache-2.0](LICENSE)。音乐播放器示例内置曲谱采用 [CC0-1.0](examples/music_player/README.md)。
