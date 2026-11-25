# Candis Dock EEPROM 读取与维护

[使用文档](README.md) · [English](../en/eeprom-programming.md)

Dock v1.2 的 U10 使用英锐芯 AT24C02 SOP-8（立创料号 `C688857`）。器件容量为 256 字节，共 32 页，字地址为 8 位，页长为 8 字节，内部写周期最大 5 ms。字段和各段 CRC 的定义见 [Candis Dock EEPROM 描述符](eeprom-v1.md)。

## 地址与访问

U10 的 A0/A1/A2 和 WP 接地，7 位地址固定为 `0x50`。U6/U7 的模块 EEPROM 地址分别保留为 `0x51` 和 `0x52`。当前 BSP 只解析 Dock 的 `0x50` 描述符，`candis_dock_probe_modules()` 对 `0x51`/`0x52` 只报告 ACK 状态。

BSP 使用随机起点顺序读。主设备先发送 START、器件写地址和 8 位存储偏移，再发送 repeated START 和器件读地址。读取起点为 `0x00`，Dock 描述符长度为 134 字节。读取范围不得超过 `0xFF` 后回绕。

Dock v1.2 的 WP 固定接地。BSP 不提供 EEPROM 写接口，正常运行只读。生产写入由受控的离线或工位流程完成。

## 写入规则

- 每笔页写只覆盖单个 8 字节页，跨页数据必须拆分。不按页边界写入会在器件内部回绕并覆盖本页数据。
- STOP 后等待内部写周期完成。下一笔事务前可执行 ACK Polling，并以目标字节回读作为最终确认。
- 写入前备份完整的 256 字节镜像，写入后重新读取并逐字节比较。
- 新空白镜像的 `0x86..0xFF` 填 `0xFF`。维护已有器件时保留该区的原始内容，除非维护工单明确要求更新。
- Vendor ID、Board Type、Product ID、Hardware Revision、Serial Number、制造字段和 Parameter Data 必须来自已审核的 Dock 产品记录。

## 镜像放行

1. 按 [描述符定义](eeprom-v1.md) 生成前 134 字节。Dock v1.1 使用 Hardware Revision `0x0101`，Dock v1.2 使用 `0x0102`。
2. 分别计算 Identity、Manufacturing 和 Parameter CRC。Parameter Length 不得超过 64。
3. 组成 256 字节器件镜像，并使用独立解析器核对字段和 CRC。
4. 写入后读取完整 256 字节，确认前 134 字节与批准镜像一致，保留区符合工单，三个 CRC 均通过。
5. 冷启动后调用 `candis_dock_probe()` 或 `candis_baseboard_probe_on_bus()`，确认调用成功且返回对应 Hardware Revision。

U6/U7 模块 EEPROM 的上层产品身份和应用配置由各模块的板级定义负责。当前 BSP 不解析这两个地址上的描述符，也不把 ACK 转换为模块驱动绑定。
