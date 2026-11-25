# Candis music player

Connect the speakers and supply ALDO1 and ALDO2. From the root of the checkout, build the example and follow [USB and flashing](../../docs/en/usb.md).

```sh
idf.py --preview -C examples/music_player build
```

Tap **Play** to loop *First Light*, or **Stop** to end playback. Adjust **Volume** and switch between **Left**, **Stereo**, and **Right** from the display. The player generates stereo PCM from the score compiled into the application, with no storage or network setup.

The default build leaves the panel and audio rails unchanged. Applications can enable rail management after configuring it for the complete assembly.

The bundled [score](main/score.h) is licensed under [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/).
