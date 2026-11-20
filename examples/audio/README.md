# Candis 双声道录音机

在 AMOLED 上点击 **Record** 开始录音，等待 **Recording** 再对麦克风发声。
点击 **Stop** 可提前结束，也可等待录满 3 秒。点击 **Play** 回放当前片段。
两个电平条分别显示左右声道在 50 ms 窗口内的 PCM 峰值，录完后提示饱和的声道。
回放电平取自送入 BSP 限幅器前的 PCM。片段以 48 kHz、16 位双声道保存在 PSRAM。
用 **Playback channel** 选择立体声、仅 L 或仅 R，可逐槽验证两路扬声器。

```sh
idf.py --preview -C examples/audio build
```

默认要求 ALDO1 和 ALDO2 已供电。已核实屏幕电气条件、麦克风和扬声器接线的综合测试板
可以在独立构建目录叠加台架配置，允许示例管理两条电源轨。

```sh
idf.py --preview -C examples/audio -B build/bench \
  -D SDKCONFIG=build/bench/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bench' build
```

录音时功放关闭。**Playback volume** 可在回放前或回放中调节，默认 50%。
0% 关闭功放，100% 对应 Candis BSP 的 ES8389 最高档（双声道 DAC −6 dB）。
回放同时采用 BSP 峰值限幅和约 5 ms 的片段边缘淡入淡出。
停止回放时，已提交的 PCM 先通过 I²S 播完，随后关闭功放并释放音频资源。
串口记录帧数、削波声道、操作结果和清理结果。

实板验收需分别确认左右麦克风和扬声器、触控停止、重复录放、听感和 ALDO2 恢复。
