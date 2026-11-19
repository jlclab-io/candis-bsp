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
| [`candis-camera`](components/candis-camera) | 通用 OV5640 DVP 摄像头组件，支持硬件配置、模式选择、拍照和连续取流 |
| [`candis_dock`](components/candis_dock) | Dock 扩展底板支持 |
| [`candis_gamepad`](components/candis_gamepad) | Gamepad 扩展板支持 |

## 构建示例

使用 ESP-IDF `release/v6.2`。CI 固定源码提交
`2b10d1278a547f5dc92efbe516144b4ae85f80cb`。ESP32-S31 在该 SDK
修订仍需 `--preview`。执行 ESP-IDF 的 `install.sh esp32s31`，并加载其 `export.sh`。

```sh
idf.py --preview -C examples/display build
idf.py --preview -C examples/diagnostics build
idf.py --preview -C examples/expansion build
idf.py --preview -C examples/camera build
idf.py --preview -C examples/rtc build
idf.py --preview -C examples/motion build
```

示例开启 PSRAM，默认采用 50 MHz QSPI 和综合测试板 USB Serial/JTAG
控制台。屏幕需由有效的 3.3 V ALDO1 电源轨供电。用 `menuconfig` 的
**Candis display example** 启用台架电源管理、80 MHz QSPI 和自动采样。
可在同一菜单通过 `CANDIS_BENCH_ASYNC_FLUSH` 开启异步刷新对照。示例日志的
`touch_poll_gap_max_us` 表示采样窗口内最长的触摸轮询间隔。
USB Serial/JTAG 与 Candis USB OTG 是两个接口，烧录时应先核对串口和分区。

摄像头示例启动后在 480×480 AMOLED 上全屏预览 OV5640 画面。综合测试板需接
摄像头和 AMOLED，默认使用 50 MHz QSPI，要求 ALDO1 已供电。已确认屏电条件的
台架可叠加 `examples/camera/sdkconfig.bench` 启用 ALDO1 管理和 80 MHz QSPI。
串口每 5 秒报告采集帧率、面板帧率与出队至面板 DMA 完成用时。

显示复测可在干净的示例配置中叠加 `sdkconfig.bench`（把 `SDKCONFIG_DEFAULTS`
设为 `sdkconfig.defaults;sdkconfig.bench`）。诊断示例的 `sdkconfig.features`
打开音频、TF 和 OTG CDC 下载支持。综合测试板 TF 需另外选择 GPIO35 卡检测并按连接负载
设置 DC1SW。扩展板示例执行显式探测，运行前须满足 GPIO58/59 与供电前置条件。

RTC 示例通过综合测试板的 USB Serial/JTAG 提供命令行。输入 `READ` 读取 UTC 日历，
`SET <UTC epoch>` 设置有效日历，`RECOVER <UTC epoch>` 允许恢复掉电后无效的日历。
在主机执行 `date -u +%s` 获取当前 epoch。示例启动时只读日历。

日历有效时，发送 `SLEEP` 测试 RTC 更新中断唤醒。示例临时屏蔽 TG28 中断，
将 GPIO0 配置为低电平 EXT1 唤醒源，并设 5 秒定时器兜底。USB Serial/JTAG
重新枚举后重新打开串口，发送 `WAKE`。成功结果为
`RTC_WAKE ESP_OK deep=1 ext1=1 guard=0 update=1 restore=ESP_OK`。
综合测试板的 CDC 唤醒后可能无法收发。此时先用匹配的 ELF 经 JTAG 读
`s_wake`，硬复位会丢失本轮结果。测量条件和判据见 [硬件与实测](docs/hardware.md)。

运动示例默认使用已经上电的扩展域。确认综合测试板的 DC1SW 负载后，可在 `menuconfig`
启用 `CANDIS_MOTION_MANAGE_POWER`。`CANDIS_MOTION_TEST_STORAGE` 额外验证连接的 TF 卡
与 IMU 并行时的供电释放顺序。

USB OTG 的应用区更新和综合测试板 USB Serial/JTAG 的恢复方式见 [USB 调试](docs/usb.md)。

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
  `candis_display_start()`。
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

GitHub Actions 构建六个独立示例、运行主机测试，并从 `compote` 生成的
`candis` 和 `candis-camera` 包分别构建独立应用。Registry 工作流通过 `IDF_COMPONENT_NAMESPACE`
仓库变量指定实际命名空间。正式上传使用 `IDF_COMPONENT_API_TOKEN` 仓库密钥。
在 `main` 手动运行 **ESP-IDF build**，默认向 Registry 提交校验且跳过版本创建。
预检通过后可启用 `publish` 发布清单中的版本。发布包附带显示示例，引用同一
Registry 命名空间下的确切版本。工作流从 Registry 下载并构建该示例，随后核对
组件清单记录的 Git 提交。命名空间尚未配置时，预检中的示例使用固定的 Git 修订。

摄像头组件使用独立清单，可单独预检和发布。手动工作流的 `publish_camera`
选项上传 `candis-camera`，随后从 Registry 下载到全新应用并验证构建和来源提交。
综合测试板预设位于摄像头示例，安装方法见 [摄像头组件](components/candis-camera)。

Dock 和 Gamepad 在仓库中通过本地路径联调。发布时，用
`python3 tools/stage_registry_extension.py <组件名> <新暂存目录> --namespace <已获批命名空间>`
生成清单。CI 检查两个扩展能否单独打包，暂存包依赖主 BSP 清单中的确切版本。
对应主 BSP 版本发布后，可对这两个扩展做上传预检、全新工程安装及对应硬件验证。
