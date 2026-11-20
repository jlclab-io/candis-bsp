# Candis API 指南

[使用文档](README.md) · [English](../en/api.md)

| 外设 | 头文件 | 入口 |
| --- | --- | --- |
| 主 I²C | [`bsp/candis.h`](../../components/candis/include/bsp/candis.h) | `candis_i2c_acquire()` / `candis_i2c_release()` |
| AMOLED 与触摸 | [`bsp/esp-bsp.h`](../../components/candis/include/bsp/esp-bsp.h) | `candis_display_start()` / `candis_display_stop()` |
| 电源 | [`bsp/candis_power.h`](../../components/candis/include/bsp/candis_power.h) | `candis_power_read()` |
| RTC | [`bsp/candis_rtc.h`](../../components/candis/include/bsp/candis_rtc.h) | `candis_rtc_get_time()` / `candis_rtc_set_time()` |
| TF 卡 | [`bsp/candis_storage.h`](../../components/candis/include/bsp/candis_storage.h) | `candis_storage_mount()` / `candis_storage_unmount()` |
| IMU | [`bsp/candis_imu.h`](../../components/candis/include/bsp/candis_imu.h) | `candis_imu_start()` / `candis_imu_stop()` |
| 音频 | [`bsp/candis_audio.h`](../../components/candis/include/bsp/candis_audio.h) | `candis_audio_open()` / `candis_audio_close()` |
| 底板 | [`bsp/candis_baseboard.h`](../../components/candis/include/bsp/candis_baseboard.h) | `candis_baseboard_probe()` |

## 显示与触摸

启用 PSRAM 和 LVGL 软件绘制。启动前核对[屏幕供电](hardware.md#供电)。默认配置用 `bsp_display_start()`，需要指定参数和获取错误码时用 `candis_display_start()`。

```c
#include "bsp/esp-bsp.h"

void app_main(void)
{
    const candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    lv_display_t *display = NULL;
    ESP_ERROR_CHECK(candis_display_start(&config, &display));
    ESP_ERROR_CHECK(bsp_display_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT);

    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(display));
    lv_label_set_text(label, "Hello, Candis!");
    lv_obj_center(label);
    bsp_display_unlock();
}
```

应用访问 LVGL 时调用 `bsp_display_lock()` 和 `bsp_display_unlock()`。持锁时可用 `candis_display_get_touch_points()` 读取最多两个触点的坐标与跟踪 ID。双触点需设置 `CONFIG_ESP_LCD_TOUCH_MAX_POINTS=2`。

异步刷新默认开启，额外占用 450 KiB PSRAM。设置 `async_flush = false` 可使用同步刷新。摄像头等整帧输入可通过 `candis_display_start_raw()` 和 `candis_display_draw_rgb565()` 使用独占面板模式。帧须为连续存放的 480 × 480 本机字节序 RGB565，绘制返回后可复用缓冲区。

停止显示前，取消使用显示对象的应用回调和定时器。在 LVGL 锁外从控制任务调用 `candis_display_stop()`。

## I²C 与电源所有权

用 `candis_i2c_acquire()` 共享 GPIO6/7 上的主 I²C。归还引用前移除应用添加的设备。切换到 LP I²C 前释放主总线全部 HP I²C 引用。PMU 由 BSP 统一管理。

用 `candis_power_read()` 查询电源轨使能状态。存储和 IMU 共用 DC1SW，应用也可通过 `candis_expansion_power_acquire()` 和 `candis_expansion_power_release()` 持有独立引用。默认沿用现有供电。应用接管整个供电域时才启用电源管理。归还应用引用前先停止相关外设。对应的最后一个持有者释放后，BSP 只恢复由自身调整过的电源设置。

## RTC

`candis_rtc_get_time()` 读取 2000–2099 年的 UTC 日历。日历停止或无效时返回 `ESP_ERR_INVALID_STATE`。用 `candis_rtc_set_time(&utc, false)` 校时，仅在恢复无效日历时传 `true`。恢复会重置闲置的 RTC 设置，后备充电开启时拒绝执行。

用 `candis_rtc_arm_update_irq()` 和 `candis_rtc_disarm_update_irq()` 管理 RTC 更新唤醒。深睡前将清零后的状态结构存入保留内存。应用配置 SoC 唤醒并协调共用 GPIO0 中断的两个器件。`restore_pending` 置位时保留状态并重试清理。见 [RTC 示例](examples.md#rtc-控制台)。

## 存储与运动传感

选择符合实物卡检测接线的存储配置。只读浏览时为 `candis_storage_mount_with_config()` 设置 `read_only = true`。挂载时禁用自动格式化。调用 `candis_storage_detect_start()` 后，可用 `candis_storage_detect_read()` 轮询，去抖由应用负责。停止卡检测和断电前，先关闭文件并卸载。

启用 `CONFIG_CANDIS_IMU` 读取加速度与角速度。样本含以 39.0625 µs 为一计数的 24 位传感器时钟，以及以微秒表示的主机读取完成时间。比较传感器计数时按 24 位回卷计算。用 `candis_imu_read_temperature()` 读取芯片内部温度。

## 音频

启用 `CONFIG_CANDIS_AUDIO` 使用 ES8389 和 I²S。数据格式为 48 kHz、有符号 16 位交织立体声 PCM。功放开启前先发送静音并设置音量。读写函数返回实际传输帧数。停止读写后再关闭编解码器并释放供电引用。

## 扩展与摄像头

- 调用 `candis_baseboard_probe()` 前释放 UART0 并确认 DC1SW 已上电。根据返回的身份选择 [Dock](../../components/candis_dock/README.md) 或 [Gamepad](../../components/candis_gamepad/README.md) 接口。描述符兼容范围见 [BSP 支持](expansion.md#bsp-支持)。
- [摄像头 API](../../components/candis-camera/README.md)由应用配置板级引脚、SCCB、时钟和电源回调。构建时选择 OV5640 模式。拍照时复制帧到应用内存，连续取流时借用并归还 DMA 帧。
