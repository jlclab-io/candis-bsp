# Examples

[Documentation](README.md) · [简体中文](../zh/examples.md)

From the root of the checkout, run `idf.py --preview -C examples/<name> build` in your ESP-IDF 6.2 environment. The ESP-IDF 6.1 compatibility profile uses the same projects. Follow [USB and flashing](usb.md) to install the example and open the application console.

## Peripherals

| Example | What to try |
| --- | --- |
| [Minimal](../../examples/candis_minimal) | Standard I²C start and stop, shared references, and release retry. |
| [Display](../../examples/display) | Try LVGL, AMOLED refresh, and touch with ALDO1 supplied. |
| [Audio recorder](../../examples/audio/README.md) | Record and play stereo clips with the touchscreen. Connect the microphones and speakers and supply ALDO1/ALDO2. |
| [Music player](../../examples/music_player/README.md) | Play the bundled score and select speaker channels. Requires display and audio power. |
| [Camera](../../examples/camera) | Capture with a DVP sensor selected in the [camera guide](../../components/candis-camera/README.md). Preview RGB565, YUV422 and grayscale on the AMOLED. |
| [TF card](../../examples/tf_card) | Display hot-plug status and the root directory on AMOLED. With `sdkconfig.write_test`, the touchscreen button or console `W` command exclusively creates a 4 KiB temporary file, writes, syncs, compares the read-back data, and deletes the file. Requires DC1SW and the configured card-detect pin. |
| [Expansion](../../examples/expansion) | Detect the Dock, browse its TF card, and check whether the reserved module EEPROM addresses acknowledge. |
| [Dock](../../examples/dock/README.md) | Identify the Dock and exercise TF, BMI270, display and audio. |
| [ADC](../../examples/adc) | Demonstrate an application-owned two-channel ADC configuration. The example defaults do not define baseboard pins. |
| [Motion](../../examples/motion) | Read BMI270 motion and temperature with an application-supplied bus, address and sampling configuration. |
| [RTC](../../examples/rtc) | Read or set UTC and try RTC update wake. See [RTC console](#rtc-console). |
| [Power](../../examples/power/README.md) | Show power and charge telemetry on AMOLED. Validate the charger before applying the default 200 mA setpoint. |
| [TG28 monitor](../../examples/tg28/README.md) | Read TG28-ESP status over I²C from a standalone application. |
| [Motor](../../examples/motor/README.md) | Demonstrate the application-supplied GPIO, LEDC and supply configuration. The example defaults do not define a baseboard motor interface. |

## Board and system

| Example | What to try |
| --- | --- |
| [Baseboard](../../examples/baseboard) | Read the recognized baseboard descriptor. Supply DC1SW and release UART0 first. |
| [GPIO](../../examples/gpio) | Toggle the onboard LED. Leave GPIO37 available on attached hardware. |
| [Wi-Fi scan](../../examples/wifi_scan) | Discover nearby access points. |
| [BLE scan](../../examples/ble_scan) | Discover nearby Bluetooth LE advertisements. |
| [Wi-Fi and BLE coexistence](../../examples/bleprph_wifi_coex/README.md) | Scan Wi-Fi while BLE advertises, connect to an access point, verify HTTP access, and receive a GATT write. |
| [Sleep](../../examples/sleep) | Try timer-woken light/deep sleep and TG28 PWRON wake through the shared SYSTEM_IRQ_N input. See [Sleep console](#sleep-console). |
| [LP I²C](../../examples/lp_i2c/README.md) | Hand GPIO6/7 I²C ownership from HP to LP during deep sleep, wake HP from the LP core, then restore HP ownership. |
| [Flash information](../../examples/flash) | Read flash identification, capacity, and the running image hash. |

## Defined board profiles

| Example | Hardware | GPIO notes |
| --- | --- | --- |
| TF card | Candis Dock or an application-defined TF assembly | The Dock uses GPIO39 for card detect. Adjust the example for application-specific wiring |
| Camera | Dock v1.2 U7 or an application-defined DVP assembly | U7 leaves RESET and PWDN to the module or adapter. Match the assembly supplies and release UART0 for SCCB. See the [camera guide](../../components/candis-camera/README.md#application-wiring) |
| Core LED | Candis core board | GPIO37, also routed to Dock U6.8 |

For Dock TF storage, keep LEFT U6 empty or leave GPIO39 high impedance. Use a FAT-formatted card. Leave GPIO37 available at U6.8 to use the core LED. The default GPIOs in the ADC and motor examples demonstrate application configuration and are not Dock board definitions.

The TF example uses a read-only VFS mount for browsing on ESP-IDF 6.2. On ESP-IDF 6.1, the example restricts browsing to directory reads.

## Sleep console

The [sleep example](../../examples/sleep) accepts single-character commands over the application console.

| Command | Action |
| --- | --- |
| `L` | Enter four-second light sleep |
| `D` | Enter four-second deep sleep |
| `P` | Arm TG28 PWRON wake on SYSTEM_IRQ_N/GPIO0 and enter deep sleep |
| `?` | Print the previous wake state |

For `P`, press PWRON after the USB device detaches. The example restores the TG28 interrupt mask and prints `PWRON_WAKE_RESULT PASS` after reconnecting the console.

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
