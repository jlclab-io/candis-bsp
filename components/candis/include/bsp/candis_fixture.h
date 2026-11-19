/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Power the OV5640 seat on the Candis validation fixture and borrow its SCCB bus.
 * Call only with the validation fixture attached. The camera component owns this
 * lease until its video device and XCLK have stopped.
 */
esp_err_t candis_fixture_camera_acquire(i2c_master_bus_handle_t *bus);
esp_err_t candis_fixture_camera_release(void);

#ifdef __cplusplus
}
#endif
