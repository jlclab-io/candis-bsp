# Candis 音乐播放器

在 AMOLED 上点 **Play**，即可循环播放约 16 秒的内置曲《First Light》。点 **Stop** 停止并释放音频资源。
曲谱随固件编译，播放时生成 48 kHz、16 位 PCM，无需 TF 卡或网络。原创曲谱采用 CC0-1.0，可自由使用、修改和发布。

- **Volume** 滑块为 0–100%，默认 50%。0% 关闭功放，100% 使用 Candis BSP 的 ES8389 最高档（双声道 DAC −6 dB）。播放中可调节。
- **Left / Stereo / Right** 随时切换扬声器。Stereo 把同一段声音送到两路，单侧模式将另一侧的 PCM 置零，便于比较音量。
- **L / R** 显示送入 BSP 播放限幅器前的 PCM 峰值。模拟输出和实际出声通过试听确认。

默认要求 ALDO1 和 ALDO2 已供电。综合测试板确认供电和扬声器接线后，可叠加台架配置，由示例管理两条电源轨。

```sh
idf.py --preview -C examples/music_player build
idf.py --preview -C examples/music_player -B "$PWD/build/music-player-fixture" \
  -D "SDKCONFIG=$PWD/build/music-player-fixture/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.bench' build
```

原生 USB OTG CDC 也可发送以换行结束的 `PLAY`、`STOP`、`VOL 0` 至 `VOL 100`、`LEFT`、`BOTH`、`RIGHT` 和 `STATUS`。
