/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
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

typedef enum {
    CANDIS_EXPANSION_ALDO3,
    CANDIS_EXPANSION_ALDO4,
    CANDIS_EXPANSION_BLDO1,
} candis_expansion_ldo_t;

typedef struct candis_expansion_ldo_lease *candis_expansion_ldo_handle_t;

/**
 * Claim an expansion LDO exclusively. The attached expansion must support the
 * requested voltage (500–3500 mV, in 100 mV steps).
 *
 * An already enabled rail is accepted only at the requested setting and stays
 * enabled on release. A non-NULL output on error retains a partial lease and
 * must be passed to candis_expansion_ldo_release() to retry cleanup.
 */
esp_err_t candis_expansion_ldo_acquire(candis_expansion_ldo_t rail, uint16_t voltage_mv, unsigned settle_ms,
                                      candis_expansion_ldo_handle_t *handle);

/** Restore the original rail state. Retry with the same handle if release fails. */
esp_err_t candis_expansion_ldo_release(candis_expansion_ldo_handle_t handle);

#ifdef __cplusplus
}
#endif
