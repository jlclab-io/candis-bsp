# Candis Gamepad

Digital input support for the Candis Gamepad expansion. Use both expansion
connectors and provide stable DC1SW and ALDO3 power throughout the input
session.

For local development, add a `path` dependency on `components/candis_gamepad`
to the application manifest. The component resolves `candis` from the same
checkout. See `examples/expansion` for a buildable application.

Release UART0 on GPIO58/59 before probing the EEPROM. Include
`bsp/candis_gamepad.h`, open the inputs with `candis_gamepad_open()` and a
verified Gamepad descriptor, poll with `candis_gamepad_read()`, and close
with `candis_gamepad_close()` before removing power. Call these APIs from
one control task. GPIO38 is shared with the BMI270 interrupt and excluded
from these digital inputs.

The descriptor parser is covered by host tests. Formal Gamepad input and
its electrical behavior still require validation on the physical expansion.
