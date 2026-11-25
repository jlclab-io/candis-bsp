/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "bsp/candis_baseboard.h"
#include "bsp/candis_types.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t candis_baseboard_probe_on_bus(i2c_master_bus_handle_t bus, uint8_t address,
                                        candis_baseboard_info_t *info);
/** Classify a board descriptor without treating an absent or unusable endpoint as a fatal probe error.
 * A valid descriptor for another board, or a descriptor with corrupt metadata, is reported as
 * CANDIS_CAPABILITY_UNSUPPORTED. A missing endpoint or an address/read NACK is reported as
 * CANDIS_CAPABILITY_NOT_PRESENT. Other bus failures remain errors.
 */
esp_err_t candis_baseboard_probe_state_on_bus(candis_baseboard_kind_t expected,
                                              i2c_master_bus_handle_t bus, uint8_t address,
                                              candis_capability_state_t *state,
                                              candis_baseboard_info_t *info);
esp_err_t candis_i2c_probe_addresses(i2c_master_bus_handle_t bus,
                                     const uint8_t *addresses, size_t count,
                                     uint32_t *responding);

#ifdef __cplusplus
}
#endif
