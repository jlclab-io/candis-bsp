# USB 下载与调试

Candis 板载 USB-C 使用 ESP32-S31 的 USB OTG。综合测试板提供独立 USB Serial/JTAG。
诊断示例可以通过板载 OTG 下载应用，并继续使用综合测试板的 USB Serial/JTAG 记录日志。

## 从应用 CDC 下载

1. 在 ESP-IDF 环境构建并首次烧录诊断示例。通过示例的 `sdkconfig.features`，或在
   `menuconfig` 开启 `CONFIG_CANDIS_DIAGNOSTICS_OTG` 和
   `CONFIG_CANDIS_DIAGNOSTICS_OTG_DOWNLOAD`。首次烧录可用 USB Serial/JTAG，
   也可手动进入板载 USB OTG ROM 下载模式。
2. 确认电脑枚举出板载应用 CDC，关闭占用该端口的终端。取得目标板 12 位 USB 序列号。
3. 用 ESP-IDF 安装的 Python 和 esptool 运行应用区烧录工具。

~~~bash
python3 tools/candis_otg_flash.py --serial <S31_USB_SERIAL> --build-dir examples/diagnostics/build
~~~

工具通过完整的 DTR/RTS 握手要求应用释放 TinyUSB 并切到 ROM，匹配原应用 USB 序列号与
物理位置，连接后复核芯片型号、ROM 接口和芯片 MAC。ROM 端校验已安装分区表，再将
镜像写到能容纳它的未加密 factory 应用分区。写入校验后清除临时下载标志并重启。

若已装分区表采用另一布局，可通过 USB Serial/JTAG 读回已核实地址的分区表扇区，并用
`--installed-table <partition-table.bin>` 提交该文件。工具仍会与实际 Flash 比对。
它只写应用区，保留 bootloader、分区表及其他分区。首次完整烧录和分区迁移使用 ESP-IDF 的
对应流程。写入失败时可在 ROM 重试，应用已损坏或无法枚举时可通过手动 BOOT 或综合测试板
的 USB Serial/JTAG 恢复。

## 实板验证范围

综合测试板上，应用 CDC 切到 ROM 后，factory 应用分区地址和边界检查通过，应用区写入哈希校验通过。
重启后的 CDC 逐字节回显 16/16 帧通过。使用与已装分区不一致的文件时，工具拒绝写入并退出 ROM。
