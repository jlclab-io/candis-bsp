# Candis stereo recorder

Connect the microphones and speakers, and confirm ALDO1 and ALDO2 are supplied. Build from the root of the checkout.

```sh
idf.py --preview -C examples/audio build
```

After [flashing](../../docs/en/usb.md), tap **Record**, speak when **Recording** appears, and tap **Stop** or wait for the three-second limit. Tap **Play** to hear the clip. Use **Playback channel** to select stereo or a single channel and **Playback volume** to adjust output. The two meters show left and right PCM peaks.

The recorder stores 48 kHz, 16-bit stereo PCM in PSRAM. The default build uses existing power. On an assembly with verified wiring and supply, `sdkconfig.bench` opts into panel and audio rail management.
