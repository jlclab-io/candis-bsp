/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the validation fixture OV5640. Use from one control task. */
esp_err_t candis_ov5640_start(void);
/** V4L2 device path. Available after successful start. */
const char *candis_ov5640_device(void);
/** Stop streaming and close application file descriptors before calling. */
esp_err_t candis_ov5640_stop(void);

#ifdef __cplusplus
}
#endif
