/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *dummy;
} bsp_touch_config_t;

typedef struct {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool manage_power; /**< Permit enabling ALDO1 after checking panel supply limits. */
} candis_touch_config_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t track_id;
} candis_touch_point_t;

/** Open CST9220 independently of LVGL using the existing display supply. */
esp_err_t bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch);
/** Release the touch handle, its IO and its display rail. */
esp_err_t bsp_touch_del(esp_lcd_touch_handle_t touch);
/** Create CST9220 with explicit Candis axis and mirror mapping. */
esp_err_t candis_touch_new_with_config(const candis_touch_config_t *config,
                                       esp_lcd_touch_handle_t *ret_touch);

/**
 * @brief Read up to two contacts from the standalone CST9220.
 * @param[in] touch Handle returned by bsp_touch_new() or candis_touch_new_with_config().
 * @param[out] points Storage for two contacts with the configured axis mapping.
 * @param[out] count Number of contacts on success.
 * @return ESP_OK on a new report, ESP_ERR_NOT_FINISHED if no report is ready,
 *         or an error from the driver. Output is set only on success.
 */
esp_err_t candis_touch_read(esp_lcd_touch_handle_t touch, candis_touch_point_t points[2], uint8_t *count);

#ifdef __cplusplus
}
#endif
