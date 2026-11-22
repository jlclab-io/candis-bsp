/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis_camera.h"

candis_camera_config_t camera_fixture_config(void);
esp_err_t camera_dock_v12_init(void);
