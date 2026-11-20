# Candis BSP

Candis 是一块采用 ESP32-S31 和 2.14 英寸 480×480 AMOLED 的开发板。
BSP 组件 `candis` 面向 Candis 硬件修订 1.2，可独立于扩展板运行。
组件软件版本使用语义化版本。

本仓库处于开发预览阶段。已集成共享主 I²C、ST7801N QSPI 显示、TE 同步、亮度、CST9220
双触点读取、RX8130CE 日历读取和 TF 卡挂载。音频和 BMI270 六轴采样可按需启用。
实板使用范围见硬件文档。整机低功耗仍需集成验证。
参见 [API 参考](docs/api.md) 和 [硬件与实测](docs/hardware.md)。

| 组件 | 用途 |
| --- | --- |
| [`candis`](components/candis) | 主 BSP，可独立使用 |
| [`tg28-esp`](components/tg28-esp) | 通用 TG28-ESP PMU 驱动 |
| [`candis-camera`](components/candis-camera) | 通用 OV5640 DVP 摄像头组件，支持硬件配置、模式选择、拍照和连续取流 |
| [`candis_dock`](components/candis_dock) | Dock 扩展底板支持 |
| [`candis_gamepad`](components/candis_gamepad) | Gamepad 扩展板支持 |

组件支持范围及实板验收差异见 [功能覆盖](docs/component-coverage.md)。

## 构建示例

使用 ESP-IDF `release/v6.2`。CI 固定源码提交
`2b10d1278a547f5dc92efbe516144b4ae85f80cb`。ESP32-S31 在该 SDK
修订仍需 `--preview`。执行 ESP-IDF 的 `install.sh esp32s31`，并加载其 `export.sh`。

```sh
idf.py --preview -C examples/display build
idf.py --preview -C examples/diagnostics build
idf.py --preview -C examples/audio build
idf.py --preview -C examples/music_player build
idf.py --preview -C examples/expansion build
idf.py --preview -C examples/camera build
idf.py --preview -C examples/rtc build
idf.py --preview -C examples/motion build
idf.py --preview -C examples/motor build
idf.py --preview -C examples/power build
idf.py --preview -C examples/tg28 build
idf.py --preview -C examples/tf_card build
idf.py --preview -C examples/adc build
```

[示例索引](examples/README.md) 列出了全部独立工程、板型、使用方法和 Bring-Up 对应项。

显示示例开启 PSRAM，默认采用 80 MHz QSPI。全部示例默认开启板载 USB-C 的
原生 OTG CDC，同时保留综合测试板的 USB Serial/JTAG 输出和恢复入口。
屏幕需由有效的 3.3 V ALDO1 电源轨供电。用 `menuconfig` 的
**Candis display example** 启用台架电源管理和自动采样，或切换到 50 MHz 做对照。
LVGL 默认异步刷新，可在同一菜单通过 `CANDIS_BENCH_SYNC_FLUSH` 切换同步刷新。示例日志的
`touch_poll_gap_max_us` 表示采样窗口内最长的触摸轮询间隔。可设置
`CANDIS_BENCH_ROUNDS` 重复局刷、全刷、垂直扫描条与停启，并比较每轮空闲堆和最大连续空闲块。
手动点击 **Scan** 可观察扫描条上下是否错位。单指或双指拖动时，触点标记会按
控制器的跟踪 ID 保持颜色。观察方法和日志字段见 [硬件与实测](docs/hardware.md#显示与触摸验收)。
台架配置还通过 FreeRTOS 空闲任务计数报告各核非空闲时间比例。
USB Serial/JTAG 与 Candis USB OTG 是两个接口，烧录时应先核对串口和分区。

摄像头示例启动后在 480×480 AMOLED 上全屏预览 OV5640 画面。综合测试板需接
摄像头和 AMOLED，默认使用 80 MHz QSPI，要求 ALDO1 已供电。已确认屏电条件的
台架可叠加 `examples/camera/sdkconfig.bench` 启用 ALDO1 管理。
串口每 5 秒报告采集帧率、面板帧率与出队至面板 DMA 完成用时。
叠加 `examples/camera/sdkconfig.still` 可拍摄单帧并在 AMOLED 上显示 5 秒。
使用 `sdkconfig.manual` 可在取景和拍照前应用并回读手动曝光、增益、白平衡和 ISP 预设。
叠加 `sdkconfig.controls` 可在预览的 CDC 控制台中实时调节 OV5640。
`sdkconfig.uyvy` 使用 BT.601 软件转换，`sdkconfig.yuyv` 请求 DVP 硬件 YUYV 转换。
配置和命令格式见 [相机组件说明](components/candis-camera/README.md)。

显示复测可在干净的示例配置中叠加 `sdkconfig.bench`（把 `SDKCONFIG_DEFAULTS`
设为 `sdkconfig.defaults;sdkconfig.bench`）。诊断示例的 `sdkconfig.features`
打开音频、TF 和 OTG CDC 字节回显。回显配置的日志走 USB Serial/JTAG。
综合测试板 TF 需另外选择 GPIO35 卡检测并按连接负载
设置 DC1SW。扩展板示例执行显式探测，运行前须满足 GPIO58/59 与供电前置条件。
Gamepad 路径显示校准后的摇杆毫伏和按键/编码器状态。Dock 路径在 60 秒内检测 TF 插拔，
只读挂载已插入的 FAT 卡，列出根目录并卸载。
叠加 `examples/expansion/sdkconfig.display` 可在已供电的 AMOLED 上查看结果。
诊断的 `sdkconfig.record` 叠加到 `sdkconfig.features` 后可录制 400 ms 并以 20% 编解码器音量回放。
独立录音示例支持 AMOLED 触控录音、停止、回放及双声道峰值显示，单段最长 3 秒。
接线和配置见 [录音机示例](examples/audio/README.md)。
独立[音乐播放器示例](examples/music_player/README.md)内置原创曲目，支持触控播放、实时音量及左右扬声器切换。

RTC 示例通过板载 OTG CDC 提供命令行。输入 `READ` 读取 UTC 日历，
`SET <UTC epoch>` 设置有效日历，`RECOVER <UTC epoch>` 允许恢复掉电后无效的日历。
在主机执行 `date -u +%s` 获取当前 epoch。示例启动时只读日历。

日历有效时，发送 `SLEEP` 测试 RTC 更新中断唤醒。示例临时屏蔽 TG28 中断，
将 GPIO0 配置为低电平 EXT1 唤醒源，并设 5 秒定时器兜底。进入深睡前 CDC
会释放 OTG，唤醒后重新枚举。重新打开串口并发送 `WAKE`。成功结果为
`RTC_WAKE ESP_OK deep=1 ext1=1 guard=0 update=1 restore=ESP_OK`。
综合测试板的 CDC 唤醒后可能无法收发。此时先用匹配的 ELF 经 JTAG 读
`s_wake`，硬复位会丢失本轮结果。测量条件和判据见 [硬件与实测](docs/hardware.md)。

运动示例默认使用已经上电的扩展域。确认综合测试板的 DC1SW 负载后，可在 `menuconfig`
启用 `CANDIS_MOTION_MANAGE_POWER`。`CANDIS_MOTION_TEST_STORAGE` 额外验证连接的 TF 卡
与 IMU 并行时的供电释放顺序。

USB OTG 的应用区更新和综合测试板 USB Serial/JTAG 的恢复方式见 [USB 调试](docs/usb.md)。

电源示例在 AMOLED 和串口显示 TG28-ESP 的充电阶段、电池电压、电量、输出和中断。
默认只读取充电配置。受监督的 50→200 mA 台架演示及其接线条件见 [电源示例](examples/power/README.md)。
独立 TG28-ESP 工程可直接编译 [串口监测示例](examples/tg28/README.md)。
综合测试板的 M1 启停和 BLDO2 恢复见 [马达示例](examples/motor/README.md)。

## 在独立应用使用

在应用组件目录（例如 `main`）创建 `idf_component.yml`。开发时用相对于
清单的本地路径。

```yaml
dependencies:
  candis:
    path: ../../candis-bsp/components/candis
```

Git 依赖使用仓库地址、组件路径和可复现的提交或 tag。把下面的
`CANDIS_COMMIT` 替换为包含此组件的可访问 Git 修订。

```yaml
dependencies:
  candis:
    git: https://github.com/jlclab-io/candis-bsp.git
    path: components/candis
    version: CANDIS_COMMIT
```

Registry 发布后，用实际获批的命名空间和已发布版本替换
`NAMESPACE`、`PUBLISHED_VERSION`。

```sh
idf.py add-dependency "NAMESPACE/candis==PUBLISHED_VERSION"
```

### 应用接口

```c
#include "bsp/esp-bsp.h"

esp_err_t app_display_start(void)
{
    candis_display_config_t cfg = CANDIS_DISPLAY_CONFIG_DEFAULT();
    lv_display_t *display;
    esp_err_t err = candis_display_start(&cfg, &display);
    if (err != ESP_OK) {
        return err;
    }
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(display));
    lv_label_set_text(label, "Candis");
    lv_obj_center(label);
    bsp_display_unlock();
    return ESP_OK;
}
```

- `bsp_display_start()` 采用默认配置。需要启动错误码或自定义配置时用
  `candis_display_start()`。在默认配置上设置 `cfg.bench_qspi_80mhz = false`
  可切换到 50 MHz，设置 `cfg.async_flush = false` 可使用同步刷新。
- 用 `bsp_display_lock()` 保护应用线程中的 LVGL 操作。触摸快照最多包含两个
  触点及跟踪 ID。配置 `CONFIG_ESP_LCD_TOUCH_MAX_POINTS=2` 可读取双触点。
- 从应用控制任务调用 `candis_display_stop()` 释放屏、触摸和本实例持有的 I²C
  引用。应用应提前取消关联的 LVGL 定时器、回调和对象引用。ESP LVGL Port
  任务作为进程级服务保留，供同一应用再次启动显示。
- 需要使用 GPIO6/7 主 I²C 时，先用 `candis_i2c_acquire()` 取得引用，移除自己的
  I²C 设备后以 `candis_i2c_release()` 配对释放。进入 LP I²C 之前需先释放全部
  HP I²C 引用。屏幕使用 SPI2 和 TG28 的 ALDO1，两者由 BSP 统一管理。

许可证为 [Apache-2.0](LICENSE)。

## 维护与发布

GitHub Actions 构建十个独立示例、运行主机测试，并从 `compote` 生成的
`tg28-esp`、`candis` 和 `candis-camera` 包分别构建独立应用。Registry 工作流通过 `IDF_COMPONENT_NAMESPACE`
仓库变量指定实际命名空间。正式上传使用 `IDF_COMPONENT_API_TOKEN` 仓库密钥。
在 `main` 手动运行 **ESP-IDF build**，默认向 Registry 提交校验且跳过版本创建。
用 `publish_tg28` 发布 PMU 驱动并验证 Registry 安装。用 `publish` 发布主 BSP，
工作流会在上传前用 Registry 上的固定版本 `tg28-esp` 构建独立显示应用。
发布包附带显示示例，引用同一
Registry 命名空间下的确切版本。工作流从 Registry 下载并构建该示例，随后核对
组件清单记录的 Git 提交，并归档显示示例的固件和依赖锁文件供实板复验。
命名空间尚未配置时，预检中的示例使用固定的 Git 修订。工作流先在临时目录
生成发布清单，将本地 PMU 依赖替换为 Registry 命名空间与固定版本。

摄像头组件使用独立清单，可单独预检和发布。手动工作流的 `publish_camera`
选项上传 `candis-camera`，随后从 Registry 下载到全新应用并验证构建和来源提交。
手动运行还会归档综合测试板的预览镜像及依赖锁文件。预检使用解包的摄像头组件，
发布运行使用从 Registry 下载的摄像头组件。预览使用 `sdkconfig.bench` 启用 ALDO1 管理，
QSPI 采用 BSP 的默认 80 MHz。安装和接线见 [摄像头组件](components/candis-camera)。

Dock 和 Gamepad 在仓库中通过本地路径联调。发布时，用
`python3 tools/stage_registry_extension.py <组件名> <新暂存目录> --namespace <已获批命名空间>`
生成清单。CI 分别解包两个扩展并构建独立应用，使用本地的主 BSP 包验证依赖版本。
手动工作流默认对两个扩展包运行 Registry 服务端预检。
对应主 BSP 版本发布后，手动工作流中的 `publish_dock` 和 `publish_gamepad` 可分别验证
正式 Registry 依赖、运行上传预检、发布组件，再从 Registry 安装并核对来源提交。
