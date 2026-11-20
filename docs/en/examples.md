# Examples

[Documentation](README.md) · [简体中文](../zh/examples.md)

From the root of the checkout, run `idf.py --preview -C examples/<name> build` in your ESP-IDF 6.2 environment. Follow [USB and flashing](usb.md) to install the example and open the application console.

## Peripherals

| Example | What to try |
| --- | --- |
| [Display](../../examples/display) | Try LVGL, AMOLED refresh, and touch with ALDO1 supplied. |
| [Audio recorder](../../examples/audio/README.md) | Record and play stereo clips with the touchscreen. Connect the microphones and speakers and supply ALDO1/ALDO2. |
| [Music player](../../examples/music_player/README.md) | Play the bundled score and select speaker channels. Requires display and audio power. |
| [Camera](../../examples/camera) | Preview OV5640 on the AMOLED. Use the camera fixture profile; still and JPEG options are in the [camera guide](../../components/candis-camera/README.md). |
| [TF card](../../examples/tf_card) | Browse a FAT-formatted card. Select the card-detect pin for the hardware and ensure DC1SW is supplied. |
| [Expansion](../../examples/expansion) | Read Gamepad input or browse Dock storage after identifying the baseboard. |
| [Dock](../../examples/dock/README.md) | Open a Dock session, browse a TF card, and use the display or speaker. |
| [ADC](../../examples/adc) | Sample two joystick axes with calibrated millivolts when available. Choose the fixture or Gamepad profile. |
| [Motion](../../examples/motion) | Sample BMI270 motion and temperature with the expansion rail already powered. |
| [RTC](../../examples/rtc) | Read or set UTC and try RTC update wake. See [RTC console](#rtc-console). |
| [Power](../../examples/power/README.md) | Show power and charge telemetry on AMOLED. The default profile reads the PMU configuration. |
| [TG28 monitor](../../examples/tg28/README.md) | Read TG28-ESP status over I²C from a standalone application. |
| [Motor](../../examples/motor/README.md) | Run a short PWM pulse on the evaluation fixture's fitted motor. |

## Board and system

| Example | What to try |
| --- | --- |
| [Baseboard](../../examples/baseboard) | Read the recognized baseboard descriptor. Supply DC1SW and release UART0 first. |
| [GPIO](../../examples/gpio) | Toggle the onboard LED. Leave GPIO37 available on attached hardware. |
| [Wi-Fi scan](../../examples/wifi_scan) | Discover nearby access points. |
| [BLE scan](../../examples/ble_scan) | Discover nearby Bluetooth LE advertisements. |
| [Sleep](../../examples/sleep) | Enter timer-woken light sleep or deep sleep and reconnect the console afterwards. |
| [Flash information](../../examples/flash) | Read flash identification, capacity, and the running image hash. |
| [Diagnostics](../../examples/diagnostics) | Check core peripherals. The features profile also enables audio, TF and CDC echo. |

## Board profiles

Build variants in separate directories with separate `SDKCONFIG` files. For example, the Gamepad analog axes are GPIO42/43 and require ALDO3. Build their profile with the command below. Use `sdkconfig.fixture` instead for the evaluation fixture's GPIO43/44 axes and DC1SW supply. Connect external analog signals only within the ADC input range and with a common ground.

```sh
idf.py --preview -C examples/adc -B "$PWD/build/adc-gamepad" \
  -D "SDKCONFIG=$PWD/build/adc-gamepad/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.gamepad' build
```

For TF card examples, verify GPIO39 on Dock or Gamepad, or GPIO35 on the evaluation fixture. Keep Dock U6 empty or confirm its GPIO39 connection stays high impedance. Use a FAT-formatted card for directory browsing.

## RTC console

The [RTC example](../../examples/rtc) reads the calendar at startup. Use newline-terminated commands over the application console.

| Command | Action |
| --- | --- |
| `READ` | Read the UTC calendar |
| `SET <epoch>` | Set a valid calendar to a UTC Unix timestamp |
| `RECOVER <epoch>` | Restore a calendar invalidated by power loss |
| `SLEEP` | Try deep sleep with RTC update wake and a timer fallback |
| `WAKE` | Query the previous wake after reconnecting the console |

On a Unix-like host, `date -u +%s` prints a UTC Unix timestamp. Use a valid calendar before entering deep sleep. The USB port may re-enumerate after wake. RTC backup retention and sleep current depend on the hardware configuration.
