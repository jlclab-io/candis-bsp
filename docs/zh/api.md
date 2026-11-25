# Candis API 指南

[使用文档](README.md) · [English](../en/api.md)

应用包含 [bsp/esp-bsp.h](../../components/candis/include/bsp/esp-bsp.h)。`BSP_CAPS_*` 宏声明当前构建提供的接口。启用 `CONFIG_CANDIS_AUDIO`、`CONFIG_CANDIS_IMU`、`CONFIG_CANDIS_USB_DEVICE` 或 `CONFIG_CANDIS_USB_HOST` 以编入相应驱动。使用扩展功能前核对 Dock 装配与所需供电。

通用接口对齐 [ESP-BSP S31 参考实现](https://github.com/espressif/esp-bsp/tree/fb5714e02cf4289d6f092f817a7ef1e0b72a49ed/bsp/esp32_s31_korvo_1)。Candis 在硬件适用范围内保持相同的 `bsp_*` 签名，通过 `candis_*` 接口提供板级能力。

| 外设 | 初始化和使用 | 释放 |
| --- | --- | --- |
| 共享 I²C | `bsp_i2c_init()` / `bsp_i2c_get_handle()` | `bsp_i2c_deinit()` |
| AMOLED 和 LVGL 触摸 | `bsp_display_start()` / `bsp_display_start_with_config()` / `bsp_display_get_input_dev()` | `candis_display_stop()` 或 `bsp_display_stop()` |
| 裸屏和独立触摸 | `bsp_display_new()` / `bsp_touch_new()` | `esp_lcd_panel_del()` / `bsp_touch_del()` |
| TF 卡 | `candis_storage_configure()` 后调用 `bsp_sdcard_mount()` 或 `candis_storage_mount_with_config()` | `bsp_sdcard_unmount()` 或 `candis_storage_unmount()` |
| ES8389 音频 | `bsp_audio_codec_speaker_init()` / `bsp_audio_codec_microphone_init()` | `bsp_audio_deinit()` |
| BMI270 | `candis_imu_start_with_config()` / `candis_imu_read()` | `candis_imu_stop()` |
| OTG CDC Device / Host | `candis_usb_device_start()` / `bsp_usb_host_start()` | `candis_usb_device_stop()` / `bsp_usb_host_stop()` |
| 电源 | `candis_power_read()` / `candis_power_read_supply()` / `candis_power_read_battery_uv()` | 只读调用自行释放 I²C 引用 |
| 马达 | `candis_motor_init()` / `candis_motor_start_amplitude()` / `candis_motor_set_amplitude()` / `candis_motor_set_frequency()` / `candis_motor_get_state()` | `candis_motor_stop()`；定时脉冲也可到时自动释放 |
| RTC 和底板 | `candis_rtc_get_time()` / `candis_baseboard_probe_on_bus()` | 单次访问 |
| 可选底板状态 | `candis_baseboard_probe_state_on_bus()` / `candis_dock_get_state()` | 返回不支持、未安装、已识别或已初始化 |

## ESP-BSP 兼容范围

| 参考接口 | Candis 状态 |
| --- | --- |
| `bsp_display_new()` | 已实现，同时提供 `bsp_display_new_with_handles()` 和 `bsp_display_delete()` |
| `bsp_display_start*()`、锁、旋转、亮度 | 已实现。`bsp_display_enter_sleep()` 和 `bsp_display_exit_sleep()` 可在不释放资源的情况下暂停和恢复屏幕及 CST9220 |
| `bsp_touch_new()` | 已实现，支持 CST9220 交换轴和镜像配置 |
| `bsp_i2c_init/get_handle/deinit` | 已实现，标准接口持有一份幂等引用 |
| `bsp_audio_init/get_codec_itf/codec_*_init` | 启用音频后提供标准签名，录放共用一份双工会话 |
| `bsp_sdcard_mount()` 和 SDMMC Host/Slot 辅助函数 | 已实现，使用应用配置的 SDMMC 插槽。`bsp_sdcard_sdspi_mount()` 返回 `ESP_ERR_NOT_SUPPORTED` |
| `bsp_usb_host_start/stop` | 支持 Host 数据角色，Type-C 电路负责 VBUS 和限流 |
| SPIFFS、Korvo 专用 ADC、LED、按键和摄像头 | 不适用于通用板级接口。应用可选择自己的分区和 IDF 外设驱动。摄像头使用独立组件 |

`BSP_CAPS_*` 是 Candis 核心板和编译选项的编译期声明。由于 TF 引脚和马达输出由应用配置，`BSP_CAPS_SDCARD` 与 `BSP_CAPS_MOTOR` 保持为 0。底板描述符由应用提供总线地址后解码，[Dock 组件](../../components/candis-dock/README.md)提供已知的 Dock 地址并校验硬件修订。`ESP_ERR_NOT_FOUND` 表示本次探测没有找到描述符，`ESP_ERR_INVALID_CRC` 表示描述符不可用，`ESP_ERR_NOT_SUPPORTED` 表示描述符格式或身份不在支持范围内。组件启动接口在所需供电或初始化所有权未建立时返回 `ESP_ERR_INVALID_STATE`。

电源域建立并释放 UART0 后使用状态接口。`PRESENT` 表示目标描述符有效但组件尚未打开，`INITIALIZED` 表示组件拥有活动会话。总线或供电错误通过返回值单独报告，不与能力状态混淆。

应用按需启动接口。释放前停止读写并移除所依赖的设备。释放报错时可重试同一个接口。返回指针的 ESP-BSP 接口在失败时返回 `NULL`。控制接口在任务上下文调用，发生竞争时返回 `ESP_ERR_INVALID_STATE`，由控制任务重试。显示控制先获取板级控制锁，再获取 LVGL 锁，最后等待 DMA。应用应在 LVGL 锁外执行生命周期操作。回调和中断不执行释放。

## 显示与触摸

启用 PSRAM 和 LVGL 软件绘制。启动前核对[屏幕供电](hardware.md#供电)。标准 `bsp_display_start()` 和 `bsp_display_start_with_config()` 启动后用 `bsp_display_backlight_on()` 或 `bsp_display_brightness_set()` 点亮。需要指定参数和获取错误码时用 `candis_display_start()`，该接口继续应用 Candis 配置中的初始亮度。

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

用 `bsp_display_start_with_config()` 设置 LVGL 任务与绘制缓冲。需要改变方向时，`bsp_display_rotate()` 通过软件旋转 RGB565 输出。`bsp_display_backlight_on()` 恢复上一次的非零亮度。

[裸屏](../../components/candis/include/bsp/display.h)和[触摸](../../components/candis/include/bsp/touch.h)可用 `CONFIG_CANDIS_NO_GRAPHIC_LIB=y` 构建，省去 LVGL 和 `esp_lvgl_port`。裸屏通过 `esp_lcd_panel_disp_on_off()` 控制可见状态，并独立设定亮度。RGB565 原生绘制为同步调用，窗口四边须为偶数。调用 `esp_lcd_panel_del()` 时一并释放借用的 IO。独立触摸用 `candis_touch_read()` 读取最多两个触点，调用 `esp_lcd_touch_del()` 释放设备和借用的 IO。触摸释放报错时，用 `bsp_touch_del(NULL)` 重试。

休眠等待异步刷新完成后暂停面板和随 LVGL 持有的触控。唤醒恢复休眠前的亮度与裸屏可见状态。切换途中通信失败时，刷新保持暂停，可重试休眠或唤醒，也可从控制任务释放显示。独立触控与裸屏可以共用 ALDO1。已启用 LVGL 触控时，第二个会话返回 `ESP_ERR_INVALID_STATE`。

## I²C 与电源所有权

标准 I²C 接口持有一个幂等引用。应用需要独立计数时使用 `candis_i2c_acquire()`。释放所属引用前，先移除应用自行添加的 I²C 设备。

用 `candis_i2c_acquire()` 共享 GPIO6/7 上的主 I²C。归还引用前移除应用添加的设备。切换到 LP I²C 前释放主总线全部 HP I²C 引用。PMU 由 BSP 统一管理。

用 `candis_power_read()` 查询电源轨使能状态。Dock、TF 和 IMU 仅在调用方启用时使用 DC1SW，存储和 IMU 不选择底板引脚或地址。应用需要多个独立引用时，用 `candis_expansion_power_claim()` 和 `candis_expansion_power_unclaim()` 管理初始值为 `NULL` 的租约句柄。`candis_expansion_power_acquire()/release()` 继续管理一份应用引用。面板和独立触控同样分别持有 ALDO1。对应的最后一个持有者释放后，BSP 恢复由自身调整过的电源设置。

## RTC

`candis_rtc_get_time()` 读取 2000–2099 年的 UTC 日历。日历停止或无效时返回 `ESP_ERR_INVALID_STATE`。用 `candis_rtc_set_time(&utc, false)` 校时，仅在恢复无效日历时传 `true`。恢复会重置闲置的 RTC 设置，后备充电开启时拒绝执行。

用 `candis_rtc_arm_update_irq()` 和 `candis_rtc_disarm_update_irq()` 管理 RTC 更新唤醒。深睡前将清零后的状态结构存入保留内存。应用配置 SoC 唤醒并协调共用 GPIO0 中断的两个器件。`restore_pending` 置位时保留状态并重试清理。见 [RTC 示例](examples.md#rtc-控制台)。

## 存储与运动传感

核心组件不指定 SDMMC 引脚或卡检测 GPIO。应用先通过 `candis_storage_config_t` 和 `candis_storage_configure()` 提供实际接线。只有底板由 DC1SW 供电且完整负载已核验时才设置 `use_dc1sw`。标准挂载目录为 `/sdcard`，可通过 `CONFIG_BSP_SD_MOUNT_POINT` 配置。

选择符合实物卡检测接线的存储配置。ESP-IDF 6.2 及以上版本可通过 `candis_storage_mount_with_config()` 设置 `read_only = true` 浏览 TF 卡。ESP-IDF 6.1 收到此请求时返回 `ESP_ERR_NOT_SUPPORTED`。挂载时禁用自动格式化。挂载期间由存储所属任务用 `candis_storage_check_card()` 轮询，并处理读写失败。检测到拔卡后，即使重插也保持 `ESP_ERR_NOT_FOUND`，直到关闭文件并卸载。应用负责关闭文件，BSP 负责驱动资源。独立卡检测的去抖由应用负责。

启用 `CONFIG_CANDIS_IMU` 读取加速度与角速度。应用通过 `candis_imu_start_with_config()` 提供已供电、归调用方所有的 I²C 总线和实际 BMI270 地址，并设置采样率与量程。地址或器件可选时先用 `candis_imu_probe()` 探测。`candis_imu_suspend()/resume()` 保留调用方的总线并进入或退出高级省电。首个样本尚未就绪时返回 `ESP_ERR_NOT_FINISHED`。样本含以 39.0625 µs 为一计数的 24 位传感器时钟，以及以微秒表示的主机读取完成时间。比较传感器计数时按 24 位回卷计算。用 `candis_imu_read_temperature()` 读取芯片内部温度。

## 音频

启用 `CONFIG_CANDIS_AUDIO`，确认 ALDO2 已供电。扬声器和麦克风接口借用同一个 ES8389 设备。双向音频共用一次 `esp_codec_dev_open()`。默认 I²S 配置为 48 kHz、16 位立体声。首次扬声器写入会先送静音，再开启功放。停止读写后关闭编解码器，再调用 `bsp_audio_deinit()` 释放资源。

Candis PCM 接口提供 48 kHz 有符号立体声读写、帧数和超时，允许一个读任务和一个写任务并行。用 `candis_audio_set_mute()` 控制静音并关闭功放。解除静音后先写入静音帧，再显式打开功放。用 `candis_audio_close()` 释放 `candis_audio_open()` 开启的会话。两种音频入口互斥，标准接口借出的编解码器句柄由 BSP 释放。

## USB 与马达

- 启用 `CONFIG_CANDIS_USB_DEVICE` 使用原生 USB CDC。应用使用非阻塞读写，自行管理 stdio 路由。
- 启用 `CONFIG_CANDIS_USB_HOST` 使用 ESP USB Host 客户端。启动 Host 前先停止 Device CDC。使用 `BSP_USB_HOST_POWER_MODE_AUTO`，限流参数传 `false`。USB-C 电路自主控制 VBUS。注销客户端后，`bsp_usb_host_stop()` 停止数据栈并释放 PHY。
- 在 `candis_motor_config_t` 中提供马达 GPIO、LEDC 模式/通道/定时器、频率、振幅对应的最小/最大 PWM duty 及可选扩展 LDO，再调用 `candis_motor_init()`。`candis_motor_start_amplitude()` 启动持续振动，`candis_motor_set_amplitude()` 可在运行中调幅，`candis_motor_set_frequency()` 可在运行中改 PWM 频率；振幅 0 会停止输出。`candis_motor_amplitude_to_duty()` 和 `candis_motor_duty_to_amplitude()` 可查询当前配置的映射。
- `candis_motor_pulse()` / `candis_motor_pulse_amplitude()` 输出 1–600 ms 定时脉冲，并在释放时恢复本次请求修改过的电源配置。用 `candis_motor_get_state()` 查询实际 duty、振幅、频率和清理状态，或用 `candis_motor_stop()` 停止持续输出、取消脉冲或重试释放。

## 扩展与摄像头

- 创建并上电应用选择的扩展总线后，调用 `candis_baseboard_probe_on_bus()` 或 `candis_baseboard_probe_state_on_bus()`。[Dock 组件](../../components/candis-dock/README.md)提供 GPIO58/59 总线、地址 `0x50` 和已知硬件修订规则。描述符兼容范围见 [BSP 支持](expansion.md#bsp-支持)。
- [摄像头 API](../../components/candis-camera/README.md)由应用配置引脚、SCCB、XCLK 和资源回调。构建时选择 Espressif DVP 传感器和模式。拍照可将帧复制到应用内存，连续取流可借用并归还 DMA 帧。
