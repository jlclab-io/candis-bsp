/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t candis_camera_sensor_read_register(uint16_t address, uint8_t *value);
esp_err_t candis_camera_sensor_write_register(uint16_t address, uint8_t value);
esp_err_t candis_ov5640_end_pending_group(void);
