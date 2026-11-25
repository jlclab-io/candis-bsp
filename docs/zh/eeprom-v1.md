# Candis Dock EEPROM 描述符

[使用文档](README.md) · [English](../en/eeprom-v1.md)

Candis Dock 在 U10 使用 AT24C02 保存整板身份。Dock v1.1 和 v1.2 共用 134 字节描述符，器件其余区域保留。Dock 地址为 `0x50`，扩展座 U6/U7 的模块 EEPROM 地址分别为 `0x51` 和 `0x52`。地址与电源接线见 [扩展硬件](expansion.md#位置与地址)。

## 线格式

描述符从 EEPROM 偏移 `0x00` 开始，到 `0x85` 结束。多字节无符号整数均为小端。Magic 为 ASCII `ESP`。当前 BSP 只接受 Candis Dock 的固定身份元组。

| 偏移 | 长度（字节） | 类型 | 字段 | 当前定义 |
| ---: | ---: | --- | --- | --- |
| `0x00` | 3 | byte[3] | Magic | ASCII `ESP`，字节 `45 53 50` |
| `0x03` | 1 | u8 | Board Type | Dock 为 `0x31` |
| `0x04` | 2 | u16 | Board ID / Product ID | Candis Dock 为 `0x0002` |
| `0x06` | 2 | u16 | Hardware Revision | v1.1 为 `0x0101`，v1.2 为 `0x0102` |
| `0x08` | 2 | u16 | SW Compatibility | `0x0100` |
| `0x0A` | 2 | u16 | Vendor ID | Candis 为 `0xCA4E` |
| `0x0C` | 4 | u32 | Board Flags | 当前只允许 bit 0，其他位为 `0` |
| `0x10` | 4 | u32 | Serial Number | 产品内制造序列号 |
| `0x14` | 32 | byte[32] | Board Name | 可打印 ASCII，右侧补 `0x00` |
| `0x34` | 2 | u16 | Identity CRC | 覆盖 `0x00..0x33` |
| `0x36` | 4 | u32 | Manufacture Date | 十进制 `YYYYMMDD` |
| `0x3A` | 2 | u16 | Batch Number | 制造批次号 |
| `0x3C` | 2 | u16 | Factory ID | 制造工厂号 |
| `0x3E` | 2 | u16 | Manufacturing CRC | 覆盖 `0x36..0x3D` |
| `0x40` | 2 | u16 | Parameter Version | 当前为 `0x0001` |
| `0x42` | 2 | u16 | Parameter Length | `0..64` 字节 |
| `0x44` | 64 | byte[64] | Parameter Data | 当前 BSP 不解释内容，只检查 Parameter Length 和该段 CRC |
| `0x84` | 2 | u16 | Parameter CRC | 覆盖 `0x40..0x83` |
| `0x86` | 122 | byte[122] | Reserved EEPROM Area | 不属于描述符，当前 BSP 忽略此区 |

CRC 使用 **CRC-16/MODBUS**。多项式为 `0x8005`，右移实现使用 `0xA001`，初值 `0xFFFF`，最终异或为 `0x0000`。三段分别从初值起算，CRC 字段本身不参与对应覆盖区。结果低字节先存。ASCII `123456789` 的校验值为 `0x4B37`，存储为 `37 4B`。

## 当前支持的身份

| Vendor ID | Board Type | Product ID | 产品 | Hardware Revision |
| --- | --- | --- | --- | --- |
| `0xCA4E` | `0x31` | `0x0002` | Candis Dock | `0x0101`、`0x0102` |

`candis_baseboard_decode()` 只接受上表身份。Dock 地址固定为 `0x50`。U6/U7 的 `0x51`、`0x52` 是模块 EEPROM 地址预留，当前 BSP 不根据这两个地址绑定产品、功能器件或驱动。

## 校验顺序

1. 读取完整 134 字节描述符。
2. 检查 Magic 为 `ESP`。
3. 分别校验 Identity、Manufacturing 和 Parameter 三段 CRC。
4. 检查 Parameter Length 不超过 64。
5. 检查 Vendor ID、SW Compatibility、Parameter Version 和 Board Flags。
6. 检查 Board Type、Product ID 与 Hardware Revision 属于当前接受的身份表。

任一步失败时，解码器不返回身份。完整器件为 256 字节，`0x86..0xFF` 位于描述符之外。当前 BSP 只读取描述符，不提供 EEPROM 写入接口。

## 格式依据

Candis Dock 使用 ESP-Mosaico V1 的字段偏移、三段 CRC 和 64 字节参数区。Candis 通过固定的 Vendor ID、Board Type、Product ID 和硬件修订限制实际可绑定的产品。[ESP-Mosaico module manager](https://github.com/esp-mosaico/esp-mosaico-bsp/tree/5674ab1f4cc250350d02e0f03c409b1877c954ca/components/mosaico_module_mgr)
