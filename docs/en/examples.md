# Examples

[Documentation](README.md) · [简体中文](../zh/examples.md)

From the root of the checkout, run `idf.py --preview -C examples/<name> build` in your ESP-IDF 6.2 environment. Follow [USB and flashing](usb.md) to install the example and open the application console.

## Peripherals

| Example | What to try |
| --- | --- |
| [Minimal](../../examples/candis_minimal) | Standard I²C start and stop, shared references, and release retry. |
| [Display](../../examples/display) | Try LVGL, AMOLED refresh, and touch with ALDO1 supplied. |
| [Audio recorder](../../examples/audio/README.md) | Record and play stereo clips with the touchscreen. Connect the microphones and speakers and supply ALDO1/ALDO2. |
| [Music player](../../examples/music_player/README.md) | Play the bundled score and select speaker channels. Requires display and audio power. |
| [Camera](../../examples/camera) | Preview OV5640 on the AMOLED. Use the camera fixture profile; still and JPEG options are in the [camera guide](../../components/candis-camera/README.md). |
| [TF card](../../examples/tf_card) | Browse a FAT-formatted card. Select the card-detect pin for the hardware and ensure DC1SW is supplied. |
| [Expansion](../../examples/expansion) | Detect the Dock, browse its TF card, and optionally probe module addresses. |
| [Dock](../../examples/dock/README.md) | Identify the Dock and exercise TF, BMI270, display and audio. |
| [ADC](../../examples/adc) | Sample two analog inputs. Defaults to the Dock LEFT header. |
| [Motion](../../examples/motion) | Sample BMI270 motion and temperature with the expansion rail already powered. |
| [RTC](../../examples/rtc) | Read or set UTC and try RTC update wake. See [RTC console](#rtc-console). |
| [Power](../../examples/power/README.md) | Show power and charge telemetry on AMOLED. The default profile reads the PMU configuration. |
| [TG28 monitor](../../examples/tg28/README.md) | Read TG28-ESP status over I²C from a standalone application. |
| [Motor](../../examples/motor/README.md) | Run a short PWM pulse on the fitted M1 motor. |

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

| Example | Hardware | GPIO notes |
| --- | --- | --- |
| TF card | Candis Dock | Card detect on GPIO39 |
| ADC | Candis Dock (LEFT header) | GPIO42 and GPIO43 at U6.20 and U6.18 |
| Camera | OV5640 module | Reset and power-down controlled per [camera wiring](../../components/candis-camera/README.md#custom-wiring) |
| LED and motor | Core board and Dock | LED on GPIO37, motor on GPIO5 |

For Dock TF storage, keep LEFT U6 empty or leave GPIO39 high impedance. Use a FAT-formatted card. Leave GPIO37 available at U6.8 to use the core LED. Connect external ADC inputs with a common ground and keep them within the configured 0 dB attenuation range.

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
