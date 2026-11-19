# API 参考

| 外设 | 头文件 | 初始化与释放 |
| --- | --- | --- |
| 主 I²C | `bsp/candis.h` | `candis_i2c_acquire()` / `candis_i2c_release()` |
| AMOLED 与触摸 | `bsp/esp-bsp.h` | `candis_display_start()` / `candis_display_stop()` |
| 电源 | `bsp/candis_power.h` | `candis_power_read()` 读取开关状态 |
| RTC | `bsp/candis_rtc.h` | `candis_rtc_get_time()` 按需读取 |
| TF | `bsp/candis_storage.h` | `candis_storage_mount()` / `candis_storage_unmount()` |
| BMI270 | `bsp/candis_imu.h` | `candis_imu_start()` / `candis_imu_stop()` |
| 音频 | `bsp/candis_audio.h` | `candis_audio_open()` / `candis_audio_close()` |
| 扩展板识别 | `bsp/candis_baseboard.h` | `candis_baseboard_probe()` 按需只读 |

## 主 I²C

GPIO6/7 为主 I²C。BSP 的设备与应用共享同一个 HP I²C 实例。每次成功获取
总线后，先移除本次添加的设备，再释放引用。切换到 LP I²C 前必须关闭正在使用
主总线的外设并释放所有引用。TG28 由 BSP 管理，共享总线上自行读写时请选用其他设备地址。

## 显示与触摸

打开 PSRAM 和 LVGL 软件绘制。`bsp_display_start()` 提供 ESP-BSP 风格的默认入口。
用 `candis_display_start()` 设置亮度、TE 与触摸选项，并获取具体错误。默认所需的
ALDO1 电源条件和台架频率见 [硬件与实测](hardware.md)。

`candis_display_config_t.async_flush` 默认为关闭。显式开启后，BSP 额外分配
450 KiB PSRAM，在独立任务中等待 TE 并传输已完成的图像。发送任务忙时，LVGL
继续合成，待发送区域合并到下一次提交。

应用任务使用 `bsp_display_lock()` 和 `bsp_display_unlock()` 操作 LVGL。
`candis_display_get_touch_points()` 在持锁时返回每个触点的坐标与跟踪 ID。
读到控制器暂未更新数据时继续保留上次报告。应用读取到零触点时应结束当前触摸。
如需停止显示，从控制任务取消应用定时器，再调用 `candis_display_stop()`。

`candis_display_get_stats()` 返回累计传输成功的帧数、像素数、帧准备时间、等 TE 时间、
传输时间及错误。用两次快照求差值计算 FPS；比较时使用相同负载、频率和屏上区域。

摄像头等连续 RGB565 源可用 `candis_display_start_raw()` 启动独占面板模式。
`candis_display_draw_rgb565()` 接收连续的 480×480 本机字节序像素。该调用复用
TE 同步和分条 DMA，返回后调用方可复用缓冲。两种显示模式都用
`candis_display_stop()` 释放，并可读取 `candis_display_get_stats()`。

## 电源与 RTC

`candis_power_read()` 读取 TG28 ALDO1、ALDO2、ALDO3 和 DC1SW 的开关位。返回值表示
寄存器状态，不能用于代替电压、电流或波形测量。显示、音频和存储在释放时只会关闭它们
主动开启的电源轨，并恢复本次改过的 LDO 电压配置。

`candis_rtc_get_time()` 读取 RX8130CE 的 UTC 日历，支持 2000–2099 年。器件报告 VLF 或 STOP 时，
读取返回 `ESP_ERR_INVALID_STATE`。用 `candis_rtc_set_time(&utc, false)` 设置有效日历，星期由日期计算。
校时期间暂停日历更新，回读确认后恢复。闹钟、定时器或更新中断在使用时拒绝校时。

首次使用或掉电使 VLF 置位时，可显式调用 `candis_rtc_set_time(&utc, true)`。该流程复位闲置的
闹钟、定时器、数字校正和后备开关配置，后备充电已开启时拒绝执行。仅在写入校验并恢复走时后
清除 VLF，保留其他待处理中断标志。RTC 与 TG28 共用 GPIO0，该流程不更改 TG28 或 SoC 的中断配置。

`candis_rtc_arm_update_irq(&state)` 在 RTC 日历有效、定时器与中断均空闲时启用每秒
更新中断。应用调用前将 `candis_rtc_update_irq_t` 清零，并将它存入可跨深睡保留的内存。
唤醒或取消睡眠时调用 `candis_rtc_disarm_update_irq(&state, &updated)`。该函数关闭
更新中断、清除 UF 并恢复原更新周期。若操作失败且 `restore_pending` 仍为真，保留
状态并重试恢复。应用负责配置 SoC 唤醒源和协调 TG28 的共享中断。判断 RTC 唤醒时，
同时检查深睡复位原因、EXT1 引脚、RTC 更新标志以及共享线上其他中断源。

## 存储

`candis_storage_mount()` 使用四位 SDMMC，挂载返回的卡由 BSP 管理。选择正式扩展板
或综合测试板对应的卡检测引脚。卸载前关闭全部文件。卡不存在、格式不兼容或驱动出错时
返回原错误。挂载配置始终禁用自动格式化。

DC1SW 同时给多个扩展设备供电。挂载时优先使用既有的电源状态。只有确认当前应用负责
整个 DC1SW 域时才可传入 `manage_power=true`。

## IMU

启用 `CONFIG_CANDIS_IMU` 后，可调用 `candis_imu_start()`、`candis_imu_read()` 和
`candis_imu_stop()`。轮询配置为 100 Hz、±4 g 和 ±1000 dps，输出使用芯片坐标系。
样本中的时间戳为本次读取完成后的 ESP 单调时钟。加速度和陀螺仪由驱动分别读取。

在扩展域已经上电时传入 `false`。台架确认整条 DC1SW 域负载后，应用可传入 `true`
管理这条电源。如果 IMU 启动或停止时报出清理失败，可再次调用 `candis_imu_stop()` 释放
剩余资源。IMU、TF 与底板识别使用同一条 DC1SW 供电。BSP 每次为外设分配一个引用，最后
一个引用释放时才恢复 BSP 自己开启的电源。外部开启的 DC1SW 保持上电。

## 音频

启用 `CONFIG_CANDIS_AUDIO` 后可通过 ES8389 和 I²S 双向传输 48 kHz、16 位交织立体声 PCM。
初始音量采用综合测试板实测通过的 100%，启动时校验两路 DAC 寄存器为 0xB3（−6 dB），
PA 保持关闭。两路麦克风采用 30.5 dB PGA。应用可以在发送静音后调低音量，再按需开启 PA。
音量设为 0 时关闭 PA。
使用 `candis_audio_playback_write()` 输入最多 240 帧，可按块采用最高 4 倍增益和 25,000
峰值限制。录音回放建议由应用在片段边界做淡入淡出。
读写分别返回实际传输帧数和状态。停止流后关闭驱动以释放 I²S 和电源。

## 扩展板

从 [硬件与实测](hardware.md) 检查 UART0、DC1SW 和扩展板前置条件。`candis_baseboard_probe()`
读取描述符并验证身份，应用据此调用 `candis_bb` 或 `candis_gamepad`。描述符内容不会配置引脚
或电源。`candis_gamepad_read()` 返回数字电平，去抖、正交码解码和摇杆轴校准由应用实现。

## 摄像头

综合测试板 OV5640 使用可选组件 `candis_ov5640`。该组件负责传感器引脚、XCLK 和上电顺序，
通过主 BSP 的扩展 LDO 租约和共享主 I²C 管理资源。应用初始化 `candis_ov5640_start()` 后，
通过 `candis_ov5640_device()` 取得 V4L2 节点。停止流、解除帧缓冲映射并关闭节点后，调用
`candis_ov5640_stop()` 释放 DVP、XCLK 和已持有的电源。示例的 800×600 RGB565 取流使用
两个 MMAP 缓冲。应用将中心 600×600 区域缩放至 480×480，再将帧送到面板。
摄像头缓冲在面板写入前归还。DQBUF 到 DMA 完成计时覆盖缩放、TE 等待和传输。

自定义硬件使用 `candis_ov5640_start_with_config()`，传入 DVP 引脚、时钟、SCCB
及电源回调。驱动检查引脚能力和重复分配。使用外借总线时，应用需协调该总线上已有资源。
关闭 `CONFIG_CANDIS_OV5640_FIXTURE` 后可独立构建摄像头组件。
