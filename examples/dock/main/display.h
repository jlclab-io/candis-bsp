/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t dock_display_start(bool touch);
esp_err_t dock_display_stop(void);
esp_err_t dock_display_mode(bool full);
void dock_display_report(void);
