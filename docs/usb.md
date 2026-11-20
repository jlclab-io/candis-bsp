# USB 下载与调试

Candis 板载 USB-C 使用 ESP32-S31 的 USB OTG。全部示例默认通过原生 CDC 输出
应用日志，并保留综合测试板独立 USB Serial/JTAG 的输出。RTC、马达和相机调参
示例从已打开的 OTG CDC 接收命令。OTG CDC 未打开时使用启动控制台作为输入。

`menuconfig` 的 **Candis example USB** 可独立关闭原生 CDC 或自动下载。
进行睡眠或电流基线测试时可关闭原生 CDC。诊断示例的 `sdkconfig.features`
把 OTG CDC 分配给字节回显，日志仍通过 USB Serial/JTAG 输出。充电测试和
马达脉冲期间自动下载暂停，状态恢复后可再次发起。相机预览收到下载请求后，
先退出取流并释放相机和屏幕资源，再交给 ROM。拍照写卡完成和卸载后再交接。
RTC 进入深睡前释放 OTG。

## 从应用 CDC 下载

1. 在 ESP-IDF 环境构建目标示例。首次迁移可用 USB Serial/JTAG，也可在板载
   USB OTG 上手动进入 ROM 下载模式。运行新版示例后可直接从应用 CDC 交接。
2. 确认电脑枚举出板载应用 CDC，关闭占用该端口的终端。取得目标板 12 位 USB 序列号。
3. 用 ESP-IDF 安装的 Python 和 esptool 运行应用区烧录工具。

~~~bash
python3 tools/candis_otg_flash.py --serial <S31_USB_SERIAL> --build-dir examples/power/build
~~~

工具通过完整的 DTR/RTS 握手要求应用释放 TinyUSB 并切到 ROM，匹配原应用 USB 序列号与
物理位置，连接后复核芯片型号、ROM 接口和芯片 MAC。ROM 端校验已安装分区表，再将
镜像写到能容纳它的未加密 factory 应用分区。写入校验后清除临时下载标志并重启。

若已装分区表采用另一布局，可通过 USB Serial/JTAG 读回已核实地址的分区表扇区，并用
`--installed-table <partition-table.bin>` 提交该文件。工具仍会与实际 Flash 比对。
它只写应用区，保留 bootloader、分区表及其他分区。首次完整烧录和分区迁移使用 ESP-IDF 的
对应流程。写入失败时可在 ROM 重试，应用已损坏或无法枚举时可通过手动 BOOT 或综合测试板
的 USB Serial/JTAG 恢复。

## 独立目录

从本仓库直接构建时，各示例共用 `examples/common/candis_example_usb`。
单独复制某个示例工程时，在目标示例目录运行前把该组件复制到其
`components/` 下，例如在本仓库根目录执行
`bash tools/copy_example_usb.sh <复制后的示例目录>`。Registry 示例包含这份公共组件。
