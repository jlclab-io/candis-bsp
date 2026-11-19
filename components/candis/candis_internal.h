/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis.h"

bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus);
esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus);

typedef enum {
    CANDIS_RAIL_DISPLAY = 0x01,
    CANDIS_RAIL_AUDIO = 0x02,
    CANDIS_RAIL_EXPANSION = 0x80,
} candis_rail_t;

typedef struct {
    candis_rail_t rail;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t pmu;
    uint8_t voltage_reg;
    uint8_t original_voltage;
    bool owned;
} candis_power_t;

/* Call with the board control lock held. Release partial acquisition on error. */
esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms);
esp_err_t candis_power_release(candis_power_t *lease);
