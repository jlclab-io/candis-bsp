/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LCD_H_RES 480
#define BSP_LCD_V_RES 480
#define BSP_I2C_SCL GPIO_NUM_6
#define BSP_I2C_SDA GPIO_NUM_7

/** A successful acquire grants one reference. Remove your I2C devices before release. */
esp_err_t candis_i2c_acquire(i2c_master_bus_handle_t *bus);
esp_err_t candis_i2c_release(i2c_master_bus_handle_t bus);

typedef struct {
    uint8_t brightness_percent;
    bool enable_touch;
    bool sync_to_te;
    /** Permit this instance to enable ALDO1 if it is off. See the hardware notes. */
    bool manage_panel_power;
    /** Request 80 MHz for the characterized bench setup. Default is 50 MHz. */
    bool bench_qspi_80mhz;
    /** Overlap LVGL and panel IO using an additional 450 KiB of PSRAM. */
    bool async_flush;
} candis_display_config_t;

#define CANDIS_DISPLAY_CONFIG_DEFAULT() {     .brightness_percent = 50, .enable_touch = true, .sync_to_te = true, }

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t track_id;
} candis_touch_point_t;

typedef struct {
    uint64_t frames;
    uint64_t pixels;
    uint64_t prepare_us;
    uint64_t transfer_us;
    uint64_t te_wait_us;
    uint32_t last_transfer_us;
    uint32_t max_transfer_us;
    uint32_t te_edges;
    uint32_t te_timeouts;
    uint32_t transfer_errors;
    uint32_t touch_errors;
} candis_display_stats_t;

/** Call lifecycle and brightness APIs from a control task outside the LVGL lock. */
esp_err_t candis_display_start(const candis_display_config_t *config, lv_display_t **display);
/** Start panel-only mode. Touch and asynchronous LVGL refresh settings are ignored. */
esp_err_t candis_display_start_raw(const candis_display_config_t *config);
/** Present a packed, native-endian 480 x 480 RGB565 frame in panel-only mode.
 * Call from a control task. The caller may reuse pixels when the call returns.
 * ESP_OK means every panel DMA strip completed.
 */
esp_err_t candis_display_draw_rgb565(const uint16_t *pixels);
esp_err_t candis_display_stop(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t candis_display_get_stats(candis_display_stats_t *stats);

/** Call with the display lock held. Capacity is two contacts. */
uint8_t candis_display_get_touch_points(candis_touch_point_t points[2]);

lv_display_t *bsp_display_start(void);
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
