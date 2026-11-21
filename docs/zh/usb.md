# USB与固件下载

[使用文档](README.md) · [English](../en/usb.md)

Candis 板载 USB-C 使用原生 USB OTG。示例启动后通常会提供 USB CDC 串口控制台。进入 ROM 下载模式或重启应用后，电脑上的串口名称可能发生变化。

## 首次烧录

1. [构建示例](../../README_ZH.md#快速开始)，接好支持数据传输的 USB 线，并关闭占用板卡串口的终端。
2. 进入 ROM 下载模式。若板卡已开机或烧录失败，先长按 **PWRON** **6 秒**关机。按住 **BOOT**，同时按下 **PWRON** **2 秒**开机。松开 **BOOT**，在电脑上确认 ESP32-S31 ROM 串口。
3. 用 ESP-IDF 烧录完整工程。将 `/dev/ttyACM0` 换成已确认的 ROM 串口。

   ```sh
   idf.py --preview -C examples/display -p /dev/ttyACM0 flash
   ```

4. 正常启动后，在串口终端打开应用 CDC 端口。若板卡仍停留在 ROM 下载模式，松开 **BOOT** 后关机再开机。

首次安装或需要变更分区布局时使用完整烧录。变更布局前先备份需要保留的数据。

## 仅更新应用

板卡正在运行兼容的 Candis 示例并已枚举原生 OTG CDC，或已进入 USB OTG ROM 模式时，可使用 [`candis_otg_flash.py`](../../tools/candis_otg_flash.py)。

1. 构建新应用并关闭串口终端。
2. 记下目标板的 USB 序列号（六个十六进制字节）。
3. 在 ESP-IDF 环境中从仓库根目录执行下列命令，并换成实际序列号与构建目录。

```sh
python3 tools/candis_otg_flash.py --serial YOUR_BOARD_SERIAL --build-dir examples/display/build
```

工具会核对板卡和已安装的分区表，只写入兼容的未加密 factory 应用分区。如果板卡使用不同的分区布局，可通过 `--installed-table` 指定已核实的分区表文件。需要迁移布局时使用 ESP-IDF 完整烧录流程。

## 找不到串口

- 检查 USB 数据线，重启后重新连接。ROM 下载和应用 CDC 的串口名称可能不同。
- 应用 CDC 不可用时，按[首次烧录](#首次烧录)中的 BOOT 和 PWRON 按键顺序进入 ROM 下载模式。
- `idf.py menuconfig` 的 **Candis example USB** 菜单可设置原生 CDC 与自动下载。独立复制示例时，用 [`copy_example_usb.sh`](../../tools/copy_example_usb.sh) 将 [`candis_example_usb`](../../examples/common/candis_example_usb) 复制到示例的 `components/` 中。

独立复制工程后，更新应用清单以解析 BSP 依赖。如果清单使用 [`candis_example_tf`](../../examples/common/candis_example_tf)，也要复制该组件并更新它的 Candis 依赖。
