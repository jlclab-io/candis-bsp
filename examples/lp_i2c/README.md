# LP I²C ownership handoff

This example verifies that the HP core and LP core can share the Candis main I²C bus on GPIO6/7 without running as I²C masters at the same time.

## Flow

1. The HP core reads the TG28 chip ID and releases the normal I²C driver.
2. The HP core configures LP I²C and starts the LP core immediately before deep sleep.
3. The LP core waits for the HP core to enter deep sleep, reads TG28 register `0x03`, and wakes the HP core.
4. The HP core stops the LP core, releases the LP GPIO configuration, and reads the chip ID again with the HP I²C driver.

Expected values are `HP_BEFORE id=0x4a`, `LP_RESULT ... id=0x4a`, `HP_RESULT id=0x4a`, and `LP_I2C_RESULT PASS`. A 20-second timer is armed as a recovery path and must not be reported as an LP wake.

Build from the repository root:

```sh
idf.py --preview -C examples/lp_i2c build
```

The example uses the Candis CB main I²C pins. Attach the verified Candis hardware and native USB OTG console before running it.
