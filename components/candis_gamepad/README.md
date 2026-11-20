# Candis Gamepad

Input support for the Candis Gamepad rev. 1.0 baseboard. Use both BTB connectors, supply DC1SW and ALDO3, and release UART0 on GPIO58/59 before identifying the board.

## Use in an application

Add a dependency on `components/candis_gamepad` and include [`bsp/candis_gamepad.h`](include/bsp/candis_gamepad.h). Verify the Gamepad descriptor before opening the inputs. Use a single control task to poll inputs and close the driver before removing power.

| Need | API or configuration |
| --- | --- |
| Raw digital inputs | `candis_gamepad_open()` and `candis_gamepad_read()` |
| Debounced edges and encoder delta | `candis_gamepad_open_with_config()` and `candis_gamepad_poll()`. Poll fast enough to see each encoder phase |
| Joystick axes | Set `axes` to use the GPIO42/43 ADC channels. Enable `calibrate_axes` for millivolts when `voltage_valid` is set. Measure center and travel on the actual joystick |
| SW5 | Set `shared_sw5` only with BMI270 INT1 high impedance and stable power throughout the session |
| Cleanup | `candis_gamepad_close()` |

[Expansion example](https://github.com/jlclab-io/candis-bsp/tree/main/examples/expansion) · [Hardware](https://github.com/jlclab-io/candis-bsp/blob/main/docs/en/expansion.md#resource-allocation) · [Apache-2.0](LICENSE)
