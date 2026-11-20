# TG28-ESP 功能覆盖

芯片行为以 TG28-ESP Datasheet V1.0 的功能章节及位域表为主，厂商《TG28 常见问题 V1.0》用于核对温度转换、ADC 溢出与睡眠顺序。Linux v1.2.1 用于核对硬件功能、调用链和变体差异。组件使用 ESP-IDF I²C master，在应用任务中调用。初始化只注册 I²C 设备，保持实际 OTP 和复位设置。

状态列描述当前实现。硬件列区分历史日志和本版待测，寄存器回读只证明芯片报告的值。危险写入以参数和传输模拟验证为主。

| 功能 | Linux 入口或定义 | TG28-ESP 依据 | ESP-IDF API 与状态 | 验证范围 |
| --- | --- | --- | --- | --- |
| 总线、识别和运行状态 | tg28.c 的 probe/init/usb_det，tg28.h，axp-core.c 的 regmap | §6.3、6.5.1，REG00–01。REG03 来自参考驱动与台架回读 | 已实现。tg28_esp.c 的 open/close、read_registers、read/check_chip_id、read_status。open 保持无事务，识别可显式调用 | 历史 ID 0x4A 和运行状态回读。当前模拟验证 ID 不匹配和无写入 |
| 上下电来源与软件控制 | tg28.c 的 power_off/init | §6.5.4、REG10、20–21 | 已实现。read_power_sources、read/set_power_control、request_power。命令写入和保护配置需显式授权 | 只读历史。关机、重启及 PWROK 实板待测 |
| PWRON/EN 与时序 | tg28-powerkey.c probe，axp-powerkey.c 的事件处理，board.dts | §6.5.4、REG22、25、27 | 已实现。read/set_power_key、shutdown_control、pwrok。PWRON/EN 模式与插入启动条件由定制 OTP 控制 | Bring-Up 有按键 IRQ 历史。新驱动的边沿、短按、长按及实际时序待测 |
| DCDC、ALDO、BLDO、CPUSLDO | tg28-regu.c/.h 的输出表，axp-regulator.c 的 set/get_voltage、enable、suspend | §6.6 表 6-3，REG80、82–85、90–98 | 已实现。tg28_esp_regulator.c 的 read/set_rail_enabled、read/set_rail_voltage。CPUSLDO 依规格及参考表使用 500–1400 mV，表中与该范围冲突的末尾码待厂商确认 | 历史 Candis 输出读取和板级 ALDO 使用。当前模拟校验 DCDC 变步进和保留位。各轨波形待测 |
| DC1SW/DC4SW 和固定 RTC 轨 | tg28-regu.c 的 DLDO/RTC 定义 | §6.6、6.12.2、REG90[7]、91[0] | DC1SW/DC4SW 已实现使能，回读电压返回上游 DCDC 设定值。REG99/9A 的通用 DLDO 调压不适用。RTC-LDO1 是 OTP 固定供电，RTC-LDO2 为定制选项 | 历史 DC1SW 板级使用。当前模拟校验 DC1SW 设定读回。DC4SW 与 RTC 轨电压待测 |
| DVM、PWM、CCM、扩频、UVP 去抖 | tg28.c init、tg28-regu.c workmode 与 DVM、axp-regulator.c 共有模式代码 | §6.6、REG80–81、83–84 | 已实现。read/set_dvm、dcdc_pwm、dcdc_control、dcdc_spread、dcdc_uvp_delay。§6.6 的基频固定，Linux 通用 frequency 代码的地址映射不适用 | API 已构建，实际转换纹波与斜率待测 |
| DCDC OVP/UVP、欠压关机与热保护 | tg28.c init 中 PWROFF/VOFF 项 | §6.5.4、REG13、22–24 | 已实现。tg28_esp_power.c 的 dcdc_protection、shutdown_control、shutdown_voltage，tg28_esp_thermal.c 的 die_protection。保留 REG23[4] | 读写及保护条件台架待测，过压、过热、欠压故障须用可限能夹具 |
| 快速上电/唤醒 | tg28.h 的 FAST_PWRON_CFG0–3 | §6.5.4、REG28–2B | 已实现。read/set_fast_sequence、read/set_fast_start。保留 REG29[1:0]、2B[5:4] | 槽位及实际电源波形待测 |
| PMU 睡眠与恢复 | tg28.c 的 init/suspend/resume；Bring-Up 11 的 sleep API | §6.5.4.4、REG26、FAQ §2.13 | 已实现。read_sleep_config、read_sleep_controls、enter_sleep、request_wakeup。软件唤醒前检查 SLEEP_EN，同一写入保持 SLEEP_EN。应用控制共享 IRQ、关轨和 SoC 睡眠 | 主机覆盖未置位、读取失败、两位同写。Bring-Up 历史仅覆盖不同实现条件下的睡眠命令结果。本版真实掉轨与恢复待测 |
| IRQ 使能、状态、回调和应答 | tg28.c IRQ 表与 wakeup、tg28-powerkey.c、tg28-chg.c IRQ handlers、axp-core.c dispatch | §6.12.1、REG40–42、48–4A | 已实现。原始位图、update_irq_events、dispatch_irq、ack_events。REG42/4A bit5 保留。按 REG48 定义及 v1.2.1 IRQ 表，第 7 位为较高警告阈值，第 6 位为较低阈值 | 模拟覆盖 W1C、掩码保留及回调。PWRON/插拔实际事件与共享 IRQ 待测 |
| 输入限制与供电路径 | tg28-charger.c USB/AC get/set_`*hold`，tg28-chg.c 的供电属性与初始化 | §6.4、6.5.3、6.8–6.9、REG00–01、12、14–16 | 已实现。read_status、read_charger、set_input_`*`_limit、batfet_power_off、min_system_voltage。RBFET 自动控制 | 历史设置值回读。实际输入限制、切换与反灌待测 |
| 充电档位、终止、电池检测 | tg28-chg.c 的 charger_sysconfig、icchg_set/suspend/shutdown/resume，tg28-charger.c 和 axp-charger.c 的属性和充电策略 | §6.7、REG18[1]、61–64、68 | 已实现。tg28_esp_charger.c 的 read_charger、set_charge_current/voltage/termination、set_precharge_current、set_charger_enabled、set_battery_detection。200 mA 编码 0x08，0x10 为 1000 mA | 模拟覆盖 CC、传输失败和保留位。历史 50→200→50 mA 为设定值回读，实际电芯电流、温升和终止待测 |
| 备用电池充电及 CHGLED | tg28.h 的 BTN_CHG_CFG，tg28-chg.c charger_sysconfig、tg28-charger.c | §6.7.5、6.12.2、REG18[2]、69、6A | 已实现。backup_charge 使能和截止电压、read/set_chgled。充电须适配可充电备用电源 | 组合输出及备用电流待专用夹具 |
| 安全定时和充电限温 | tg28-chg.c 的 charger_sysconfig、get_property；tg28-charger.c 的 charger_init | §6.7.4、REG65、67 | 已实现。read/set_safety_timer、read/set_charge_thermal_limit，写入需显式授权 | 读取与实际限温、定时器行为待测 |
| TS、JEITA 和电池温度 | tg28-charger.c 的 TS 读取/换算分支、axp-charger.c 的温度表、tg28-chg.c 初始化 | §6.7.4、REG50、52–5D；FAQ §2.7、2.12 | 已实现。TS 模式、偏置、迟滞、温度阈值、JEITA 开关/限档，传入板级曲线的 read_ts_temperature，read_ts_override_uv。REG5D 的 RO 标记与 MCU 写入描述矛盾，暂不写入 | 主机覆盖曲线与溢出。真实 NTC、阈值与 JEITA 降额待测。Candis 固定电阻不能提供真实电芯温度 |
| ADC 和芯片温度 | tg28-chg.c 的 read_vbat/read_temp，tg28-charger.c 的 VBUS/VSYS/TS，tg28-powerkey.c ADC 单通道优先 | §6.10、REG30、34–3D；FAQ §2.8–2.9 | 已实现。VBAT/TS/VBUS/VSYS/DIE 五路原始值与电压，read_die_temperature 返回按 FAQ 计算的估计值。TG28-ESP 仅标出 GPADC 使能，Linux 的 REG3E–3F、31–33 未在 ESP 版位域表及引脚表确认 | 主机覆盖高低字节和温度换算。Bring-Up 有部分 ADC 回读，实际增益、采样时序、温度对照待测 |
| 电量计与告警 | tg28-chg.c 的 read_soc、read/set_lowsocth、reset_gauge | §6.11、REG17–18、1A、A2、A4 | 已实现。read_soc、read/set_soc_thresholds、set_fuel_gauge、reset_fuel_gauge、read_gauge_config。REG17[2] 的独立复位行为与副作用仍需厂商说明，现有命令保留该位 | 主机覆盖配置回读。SOC 精度、告警阈值和恢复待测 |
| 模型写入与剩余时间 | tg28-chg.c 的 model_update/check、read_time2empty/full；tg28.h | §6.11、REG A1–A2 | 模型写入待厂商确认。A1 位域标 RO 而功能段描述可写，需明确版本、数据格式和异常恢复。A2 bit4 按 ESP 手册解码为 ROM/SRAM，不能用作模型有效标志。A3、A6–A9 不在 ESP 版可用寄存器中 | SRAM 源可只读诊断。无对应量产电芯模型或可信剩余时间实测 |
| 数据缓冲和 GPIO1 | tg28.h、tg28.c power_off、axp-gpio.c/.h | REG04–07、1B | 已实现。read/write_buffer、read/set_gpio1。GPIO1 仅低电平或高阻，适用于接出该功能的封装。Linux REG FF、GPIO2–4 或 GPIO 中断未在 ESP 版定义 | 独立 Example 可选择缓冲读写恢复。缓冲、开漏波形待实板 |
| 看门狗 | tg28-chg.c init/debug、tg28.c IRQ map、tg28.h | §6.5.4.5、REG18[0]、19、42/4A[7] | 已实现。read/configure_watchdog、set_watchdog_enabled、feed_watchdog。启用已设为重启的 watchdog 或选择重启模式需显式授权 | 主机覆盖编码与防误启，实板先测 IRQ-only |
| 其它变体/生产项 | tg28-regu.c 的 DCDC5，tg28.h 的 FAULT、BUS、CHG_FREQ/TEST、DCDC_OC、Gauge trim/debug 宏 | §4 引脚表、§6.6 及 §6.13.2 | ESP 版不映射 Linux 五路 DCDC、RSB 切换、非 ESP 调试/修调寄存器。OTP/eFuse 属生产配置，无驱动写入入口 | 需厂商提供具体料号与正式寄存器说明 |
| Linux 平台机制和策略 | axp-core、axp-gpio、axp-powerkey、axp-charger、axp-regulator、axp-sysfs、axp-virtual、nmi/sunxi-nmi、virtual28/virtual28_dev、aw_pm.h、axp_depend.h、board.dts 与两级 Makefile/Kconfig | ESP-IDF I²C master、GPIO 与应用任务 | 平台适配。Linux 的 MFD、RSB、NMI、sysfs、input、power_supply、虚拟消费者、DTS 依赖与内核线程由调用方用 ESP-IDF 对应机制组合。SOC 平滑、插入启动策略、挂起限流、全系统电源依赖由应用负责 | 独立打包消费者构建。采用同一物理设备所有者序列化共享访问 |

参考包 32 个文件。芯片相关实现覆盖 `tg28/` 的 11 个源码与头文件，父目录的共享 AXP 驱动、NMI、平台头文件、构建表和 DTS 用于追踪实际启用路径。AXP Makefile 选用 `tg28-chg.c`，同目录 `tg28-charger.c` 是另一实现，仍纳入差异核对。旧 USB 电流 getter 无有效采样，旧故障状态宏也与 ESP 版 REG00 定义冲突。限流/恒流寄存器只提供设定值，保护事件从 IRQ 状态读取。

示例和验收办法见 [独立监测](../examples/tg28/README.md) 与 [Candis 电源监测](../examples/power/README.md)。厂商 GPL 参考源码、DTS、随附模型数据和私有手册不进入组件包。
