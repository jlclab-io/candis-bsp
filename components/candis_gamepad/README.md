# Candis Gamepad

Input support for the Candis Gamepad expansion. Use both expansion
connectors and provide stable DC1SW and ALDO3 power throughout the input
session.

For local development, add a `path` dependency on `components/candis_gamepad`
to the application manifest. The component resolves `candis` from the same
checkout. See `examples/expansion` for a buildable application.

Release UART0 on GPIO58/59 before probing the EEPROM. Include
`bsp/candis_gamepad.h`, open the inputs with `candis_gamepad_open()` and a
verified Gamepad descriptor, poll with `candis_gamepad_read()`, and close
with `candis_gamepad_close()` before removing power. Call these APIs from
one control task.

For joystick axes and processed input, pass a `candis_gamepad_config_t` to
`candis_gamepad_open_with_config()`. Set `axes` to reserve the GPIO42/43 ADC
unit. Poll with `candis_gamepad_poll()` at a cadence that covers all encoder
phases. Samples contain raw ADC readings, a button mask, debounced edge masks
and a signed encoder delta. Each delta counts a complete A/B cycle.
Set `calibrate_axes` alongside `axes` to obtain calibrated millivolts in
`x_mv` and `y_mv` when `voltage_valid` is set. The returned raw values
are ESP32-S31 ADC comparator vectors. Use the measured center and travel
on the actual joystick to map millivolts to movement. The expansion
example shows calibrated voltages and logs both values and raw vectors.

The SW5 input on GPIO38 is opt-in through `shared_sw5`. The application
must keep BMI270 INT1 high impedance and both expansion rails stable for
the full session. The default config samples the other inputs.

Host tests cover the descriptor parser, button bounce and quadrature changes.
Physical Gamepad and electrical validation remain pending.
