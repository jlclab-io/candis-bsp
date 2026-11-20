/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis.h"
#include "candis_board.h"
#include "tg28_esp.h"

bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus);
esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus);

typedef enum {
    CANDIS_RAIL_DISPLAY = TG28_ESP_ENABLE_ALDO1,
    CANDIS_RAIL_AUDIO = TG28_ESP_ENABLE_ALDO2,
    CANDIS_RAIL_ALDO3 = TG28_ESP_ENABLE_ALDO3,
    CANDIS_RAIL_ALDO4 = TG28_ESP_ENABLE_ALDO4,
    CANDIS_RAIL_BLDO1 = TG28_ESP_ENABLE_BLDO1,
    CANDIS_RAIL_BLDO2 = TG28_ESP_ENABLE_BLDO2,
    CANDIS_RAIL_EXPANSION = TG28_ESP_ENABLE_DC1SW,
} candis_rail_t;

typedef struct {
    candis_rail_t rail;
    i2c_master_bus_handle_t bus;
    tg28_esp_t pmu;
    tg28_esp_ldo_t changed_ldo;
    uint8_t original_voltage;
    bool owned;
} candis_power_t;

/* Call with the board control lock held. Release partial acquisition on error. */
esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms);
esp_err_t candis_power_release(candis_power_t *lease);
