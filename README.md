# Candis BSP

Candis 是一块采用 ESP32-S31 和 2.14 英寸 480×480 AMOLED 的开发板。
BSP 组件 `candis` 面向 Candis 硬件修订 1.2，可独立于扩展板运行。
组件软件版本使用语义化版本。

本仓库处于开发预览阶段。已集成共享主 I²C、ST7801N QSPI 显示、TE 同步、亮度、CST9220
双触点读取、RX8130CE 日历读取和 TF 卡挂载。音频为可选功能。BB 和 Gamepad 作为独立组件，
综合测试板 OV5640 提供可选 DVP 组件。实板使用范围见硬件文档。IMU 和整机低功耗仍需集成验证。
参见 [API 参考](docs/api.md) 和 [硬件与实测](docs/hardware.md)。

## 构建示例

使用 ESP-IDF `release/v6.2`。CI 固定源码提交
`2b10d1278a547f5dc92efbe516144b4ae85f80cb`。ESP32-S31 在该 SDK
修订仍需 `--preview`。执行 ESP-IDF 的 `install.sh esp32s31`，并加载其 `export.sh`。

```sh
idf.py --preview -C examples/display build
idf.py --preview -C examples/diagnostics build
idf.py --preview -C examples/expansion build
idf.py --preview -C examples/camera build
```

示例开启 PSRAM，默认采用 50 MHz QSPI 和综合测试板 USB Serial/JTAG
控制台。屏幕需由有效的 3.3 V ALDO1 电源轨供电。用 `menuconfig` 的
**Candis display example** 启用台架电源管理、80 MHz QSPI 和自动采样。
USB Serial/JTAG 与 Candis USB OTG 是两个接口，烧录时应先核对串口和分区。

显示复测可在干净的示例配置中叠加 `sdkconfig.bench`（把 `SDKCONFIG_DEFAULTS`
设为 `sdkconfig.defaults;sdkconfig.bench`）。诊断示例的 `sdkconfig.features`
打开音频、TF 和 OTG CDC。综合测试板 TF 需另外选择 GPIO35 卡检测并按连接负载
设置 DC1SW。扩展板示例执行显式探测，运行前须满足 GPIO58/59 与供电前置条件。

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
