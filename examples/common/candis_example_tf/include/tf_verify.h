/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

#define CANDIS_EXAMPLE_TF_PATH "/sdcard"

/* Run during a writable mount with card detection active. */
esp_err_t candis_example_tf_verify(void);
