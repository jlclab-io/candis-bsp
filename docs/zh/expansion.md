# Candis 扩展识别与资源管理

[使用文档](README.md) · [English](../en/expansion.md)

Candis 核心板通过 30Pin CN1 和 50Pin CN2 连接一块 BTB 底板。当前开源 BSP 支持 Candis Dock v1.1 和 v1.2。核心 `candis` 组件负责底板描述符解码，`candis-dock` 组件定义 Dock 的固定总线、EEPROM、TF 卡、BMI270 和扬声器资源。

```text
Candis 核心板
  CN1 + CN2 → Dock
               ├ U10 EEPROM → 0x50
               ├ LEFT  U6   → 0x51
               └ RIGHT U7   → 0x52
```

## EEPROM 接线

Dock v1.2 的 U10 为英锐芯 AT24C02，立创料号 `C688857`。U10 的 1/2/3 脚 A0/A1/A2、4 脚 GND 和 7 脚 WP 接地，7 位地址为 `0x50`。8 脚 VCC 接 `TG28_DC1SW`。5/6 脚分别为 `EEPROM_SDA`、`EEPROM_SCL`，经 Q3（2N7002DW）接 `EXP_SDA`、`EXP_SCL`。Q3 两个栅极和 R21/R22 两个 4.7 kΩ 上拉均接 DC1SW。Dock 的 U6/U7 共接 Q3 的 `EXP_*` 侧。核心板本身没有底板描述符器件。

## BTB 针脚

CB 用 `C41421082`（CN1）与 `C41421084`（CN2）。Dock v1.2 用 `C41421022` 与 `C41421024`。下表依据原理图、PCB 网表和连接器方向整理。每行两列有多针时按同列顺序一一对应。例如 CB CN2.11/12 对应底板 CN2.15/14，底板本地 CN2.11/12 接 VBAT。装配时按对应端 Pin 1 与位号确认。

### CN1

| CB 脚号 | Dock v1.2 脚号 | 网络 |
| --- | --- | --- |
| 1、2 | 29、27 | `RSPK_N` |
| 3 | 25 | `GPIO42` |
| 4 | 23 | `GPIO43` |
| 5 | 21 | `GPIO44` |
| 6 | 19 | `GPIO45` |
| 7 | 17 | `GPIO40` |
| 8 | 15 | `GPIO39` |
| 9 | 13 | `GPIO38` |
| 10 | 11 | `GPIO37` |
| 11 | 9 | `GPIO36` |
| 12 | 7 | `GPIO35` |
| 13、18 | 5、6 | `GND` |
| 14、15 | 3、1 | `LSPK_N` |
| 16、17 | 2、4 | `LSPK_P` |
| 19 | 8 | `GPIO5` |
| 20 | 10 | `ESP32S31_CHIP_PU` |
| 21 | 12 | `GPIO20` |
| 22 | 14 | `GPIO21` |
| 23 | 16 | `GPIO22` |
| 24 | 18 | `GPIO23` |
| 25 | 20 | `GPIO24` |
| 26 | 22 | `GPIO25` |
| 27 | 24 | `GPIO33` |
| 28 | 26 | `GPIO34` |
| 29、30 | 28、30 | `RSPK_P` |

### CN2

| CB 脚号 | Dock v1.2 脚号 | 网络 |
| --- | --- | --- |
| 1 | 25 | `GPIO48` |
| 2 | 24 | `GPIO49` |
| 3 | 23 | `GPIO50` |
| 4 | 22 | `GPIO51` |
| 5 | 21 | `GPIO52` |
| 6 | 20 | `GPIO53` |
| 7 | 19 | `GPIO54` |
| 8 | 18 | `GPIO55` |
| 9 | 17 | `GPIO56` |
| 10 | 16 | `GPIO57` |
| 11 | 15 | `GPIO58` → `EXP_SCL` |
| 12 | 14 | `GPIO59` → `EXP_SDA` |
| 13 | 13 | `TG28_NTC` |
| 14、15、16、17 | 12、11、10、9 | `TG28_VBAT+` |
| 18、19 | 8、7 | `TG28_DC1SW` |
| 20、21 | 6、5 | `TG28_ALDO3` |
| 22、23 | 4、3 | `TG28_ALDO4` |
| 24、25 | 2、1 | `TG28_BLDO2` |
| 26 | 26 | `ESP32S31_I2C_SCL` |
| 27 | 27 | `ESP32S31_I2C_SDA` |
| 28、29 | 28、29 | `TG28_BLDO1` |
| 30、31、32、33、37、38、39、40、44 | 30、31、32、33、37、38、39、40、44 | `GND` |
| 34、35、36 | 34、35、36 | `TG28_VSYS` |
| 41 | 41 | `TG28_PWRON` |
| 42、43 | 42、43 | `TG28_VRTC` |
| 45 | 45 | `GPIO47` |
| 46 | 46 | `GPIO46` |
| 47、48、49、50 | 47、48、49、50 | `BB_5V_IN` |

### Dock 20Pin

Dock v1.2 上的 LEFT 为 U6，RIGHT 为 U7，均为 `C46635855` 双排 20Pin。

| 脚号 | LEFT（U6） | RIGHT（U7） |
| ---: | --- | --- |
| 1 | `GND` | `GPIO49` |
| 2 | `GND` | `GPIO48` |
| 3 | `GPIO33` | `GPIO50` |
| 4 | `GPIO35` | `GPIO46` |
| 5 | `GPIO34` | `GPIO51` |
| 6 | `GPIO36` | `GPIO47` |
| 7 | `TG28_ALDO4` | `GPIO52` |
| 8 | `GPIO37` | `TG28_BLDO1` |
| 9 | `TG28_ALDO3` | `GPIO53` |
| 10 | `GPIO39` | `TG28_DC1SW` |
| 11 | `TG28_DC1SW` | `GPIO54` |
| 12 | `GPIO40` | `TG28_ALDO3` |
| 13 | `TG28_BLDO1` | `GPIO55` |
| 14 | `GPIO45` | `TG28_ALDO4` |
| 15 | `EXP_SDA` | `GPIO56` |
| 16 | `GPIO44` | `EXP_SCL` |
| 17 | `EXP_SCL` | `GPIO57` |
| 18 | `GPIO43` | `EXP_SDA` |
| 19 | `GND` | `GPIO35` |
| 20 | `GPIO42` | `GND` |

两槽各自的 GPIO35、`EXP_SCL`、`EXP_SDA`、DC1SW、ALDO3、ALDO4、BLDO1 和地分别直连共用。U6.10 的 GPIO39 另接 Dock 卡座的 CD 及其上拉。两槽按整条电源域供电，共用总线与地址空间。Q3 属于 U10 支路，U6 与 U7 之间保持直连。

U6 和 U7 是可配置扩展座。BSP 不把任一地址绑定到具体模块型号、功能器件或驱动。应用使用扩展座时，应在自己的板级配置中定义模块身份、功能地址、GPIO、供电和资源互斥关系。

Dock v1.2 的 U7 可按摄像头示例的 8 位 DVP 配置使用。GPIO46–53 对应 D0–D7，GPIO54、GPIO55、GPIO56、GPIO57 依次为 PCLK、XCLK、VSYNC、HSYNC。SCCB 共用 `EXP_SCL` 和 `EXP_SDA`。U7 没有专用 RESET 或 PWDN 引脚。

## 位置与地址

| 位置 | 7 位地址 | A2/A1/A0 | 依据 |
| --- | --- | --- | --- |
| BTB U10 | `0x50` | `000` | Dock v1.2 网表确认 |
| Dock LEFT（U6） | `0x51` | `001` | 本规范约定，由模块或转接板接线落实 |
| Dock RIGHT（U7） | `0x52` | `010` | 本规范约定，由模块或转接板接线落实 |

`0x50` 是 Dock U10 的固定地址。`0x51` 和 `0x52` 是 Candis 扩展规范为 U6 和 U7 预留的模块 EEPROM 地址。模块或转接板须在断电时按插槽设置 A2/A1/A0。该地址只表示模块 EEPROM 的通信位置，不定义模块产品、功能器件或驱动。

## 识别接口

- Dock 的 DC1SW 上电且 GPIO58/59 已释放 UART0 后，可调用 `candis_dock_probe()` 读取并校验 `0x50` 的 Dock 描述符。`candis_dock_get_state()` 用于区分未检测到、已识别和已初始化三种状态。
- 应用自行提供 I²C 总线和地址时，可调用 `candis_baseboard_probe_on_bus()`。当前描述符解码器只接受 Candis Dock v1.1/v1.2 的已定义身份。
- `candis_dock_probe_modules()` 仅返回 U6/U7 预留 EEPROM 地址是否有 ACK。ACK 不代表模块身份或驱动已经建立，模块自己的配置与资源分配由应用负责。

## 资源定义

| 范围 | 已定义资源 |
| --- | --- |
| 核心板 | 主 I²C0 接 TG28、RTC、触摸和音频。显示、USB 与下载路径按核心配置保留。无底板时，核心组件不选择底板外设 |
| Candis Dock | I²C1 使用 GPIO58/59，U10 地址为 `0x50`。TF 使用 GPIO20–25，卡检测使用 GPIO39。BMI270 经 Q2 接主 I²C0，地址为 `0x68`。CN3 在 v1.1 使用左声道，在 v1.2 使用右声道 |
| Dock 扩展座 | U6/U7 的针脚与共用电源见上表。BSP 不预设模块型号、功能 GPIO、功能 I²C 地址或驱动 |

GPIO39 同时连接 Dock 卡检测和 U6.10。使用 TF 卡检测时，U6 侧必须保持 GPIO39 高阻。GPIO35 由 U6.4 和 U7.19 共用。扩展 I²C 与 UART0 共用 GPIO58/59，使用 `0x50`、`0x51` 或 `0x52` 前必须释放 UART0。

DC1SW 为 TF、U10、BMI270 和 U6/U7 共用电源域，TG28-ESP v1.0 数据表中的额定值为 300 mA。应用应把稳态负载、启动浪涌和余量纳入整个电源域的预算。

## 插拔约束

Dock 和 U6/U7 扩展模块不支持带电插拔，也不支持运行时重新绑定。连接或更换底板、模块和扬声器前，应断开 USB、电池及外部供电。应用在启动后按已配置的装配读取描述符，运行期间不更换设备实例。

## BSP 支持

- `candis_baseboard_decode()` 校验 134 字节 Dock 描述符的三段 CRC 和已支持的身份元组。
- `candis_baseboard_probe_on_bus()` 在调用方提供的总线和地址上读取描述符。`candis_baseboard_probe_state_on_bus()` 返回对应能力状态。
- `candis_dock_probe()`、`candis_dock_get_state()` 和 `candis_dock_open()` 使用 Dock 固定的 I²C1 与 `0x50`。
- `candis_dock_probe_modules()` 只探测规范保留的 `0x51` 和 `0x52`。

描述符字段和当前接受的 Dock 身份定义见 [Candis Dock EEPROM](eeprom-v1.md)。
