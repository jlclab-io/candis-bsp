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

`candis_display_config_t.async_flush` 默认开启。BSP 额外分配
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

需要只读浏览时，用 `candis_storage_mount_with_config()` 传入 `read_only = true`。
已供电时可用 `candis_storage_detect_start()` 取得 DC1SW 引用和低有效卡检测输入，
再用 `candis_storage_detect_read()` 轮询。调用方负责去抖。检测期间可挂载同一板型，
卸载后仍可读检测输入。结束时先关闭文件并卸载，再调用 `candis_storage_detect_stop()`。

DC1SW 同时给多个扩展设备供电。挂载时优先使用既有的电源状态。只有确认当前应用负责
整个 DC1SW 域时才可传入 `manage_power=true`。
应用可以用 `candis_expansion_power_acquire(true)` 为一组共享 DC1SW 的外设持有
一个独立引用。先停止用电外设并释放它们的引用，再调用
`candis_expansion_power_release()`。最后一个引用会恢复由 BSP 改动的电源状态。
获取或释放失败时，可重试释放以完成清理。

## IMU

启用 `CONFIG_CANDIS_IMU` 后，可调用 `candis_imu_start()`、`candis_imu_read()` 和
`candis_imu_stop()`。轮询配置为 100 Hz、±4 g 和 ±1000 dps，输出使用芯片坐标系。
六轴数据与 BMI270 的 24 位计数器在一次 I²C 突发读取内获取。样本的 `sensor_ticks`
每个计数对应 39.0625 µs，差值按 24 位回卷计算。`timestamp_us` 为读完数据时的 ESP
单调时钟。设备计数器对应读取时刻，采样周期仍取决于配置的输出频率。
`candis_imu_read_temperature()` 读取 BMI270 芯片内部温度，首个转换尚未完成时返回
`ESP_ERR_NOT_FINISHED`。

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
读取描述符并验证身份，应用据此调用 `candis_dock` 或 `candis_gamepad`。描述符内容不会配置引脚
或电源。`candis_gamepad_read()` 保留原始数字采样。`candis_gamepad_open_with_config()`
可选取 GPIO42/43 的 ADC，并通过 `candis_gamepad_poll()` 返回按键边沿、编码器增量和原始轴值。
默认去抖时间为 10 ms，由调用方按编码器相位变化速度选择采样频率。轴标定使用实物中心、行程和方向。
应用确认 BMI270 INT1 全程高阻后，可显式打开共线的 SW5。

Dock 默认沿用已供电的 DC1SW。已测量整个扩展域负载的应用可使用 `candis_dock_mount_with_power()`
按需取得并归还供电资源。挂载前确认 GPIO39 可作为卡检测输入。关闭文件后调用
`candis_storage_unmount()`。TF 与 U6 插座共用 GPIO39。`candis_dock_detect_start()`
验证 Dock 描述符后取得卡检测输入。扩展示例使用只读挂载并在浏览后释放该次挂载。
模块接线确认后，可用 `candis_dock_probe_modules()` 检查 `0x51` 和 `0x52` 的 ACK。
扩展 I²C 共用 GPIO58/59，沿用探测底板时的 UART0 释放条件。模块型号按其描述符确认。

## 摄像头

`candis-camera` 提供配置式 DVP 摄像头接口，当前支持 OV5640。用 `candis_camera_get_mode()`
枚举构建时启用的模式。OV5640 的工作模式通过 Kconfig 选择，
`candis_camera_stream_start()` 可校验已启用模式，并配置输出格式和超时。
单帧拍照用 `candis_camera_capture_frame()` 复制到应用缓冲。连续取流时用
`candis_camera_frame_acquire()` 和 `candis_camera_frame_release()` 借还 DMA 帧。
`candis_camera_stream_stop()` 释放取流资源，`candis_camera_stop()` 进一步释放
DVP、XCLK 和电源。取流期间可在两帧之间读取芯片 ID、设置横向或纵向翻转，
以及启用传感器测试图。需要原生 V4L2 控件时可通过 `candis_camera_device()` 获取节点。

自定义硬件使用 `candis_camera_init()`，传入 DVP 引脚、时钟、SCCB
及电源回调。驱动检查引脚能力和重复分配。使用外借总线时，应用需协调该总线上已有资源。
ESP Video 探测编译时启用的 DVP 传感器。摄像头组件可独立构建。
综合测试板预设见仓库的摄像头示例。AMOLED 预览示例把
800×600 RGB565 帧裁切缩放至 480×480，在写入面板前归还摄像头缓冲。
