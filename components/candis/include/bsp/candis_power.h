/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool panel;
    bool audio;
    bool aldo3;
    bool dc1sw;
} candis_power_state_t;

/** Snapshot board rails. This call does not write the PMU. */
esp_err_t candis_power_read(candis_power_state_t *state);

#ifdef __cplusplus
}
#endif
