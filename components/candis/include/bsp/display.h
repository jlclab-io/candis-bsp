/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/config.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#if !BSP_CONFIG_NO_GRAPHIC_LIB
#include "esp_lvgl_port.h"
#include "lvgl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LCD_COLOR_FORMAT_RGB565 (1)
#define ESP_LCD_COLOR_FORMAT_RGB888 (2)

#define BSP_LCD_H_RES (480)
#define BSP_LCD_V_RES (480)
#define BSP_LCD_BITS_PER_PIXEL (16)
#define BSP_LCD_COLOR_FORMAT (ESP_LCD_COLOR_FORMAT_RGB565)
#define BSP_LCD_COLOR_SPACE (LCD_RGB_ELEMENT_ORDER_RGB)
#define BSP_LCD_BIGENDIAN (0)

typedef struct {
    int dummy;
} bsp_display_config_t;

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_handle_t control;
} bsp_lcd_handles_t;

#if !BSP_CONFIG_NO_GRAPHIC_LIB
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;
    uint32_t buffer_size;
    bool double_buffer;
    struct {
        unsigned int buff_dma : 1;
        unsigned int buff_spiram : 1;
        unsigned int sw_rotate : 1; /**< Candis always uses software rotation. */
    } flags;
} bsp_display_cfg_t;
#endif

/**
 * @brief Create the native RGB565 panel on an already powered rail.
 * @note Call esp_lcd_panel_disp_on_off() and set brightness explicitly.
 *       Draw coordinates use exclusive ends. All four edges must be even.
 *       The BSP owns the IO and SPI bus. Release via esp_lcd_panel_del() or
 *       bsp_display_delete(), which both release the panel and its resources.
 */
esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);
/** Create the native panel and return all handles owned by the BSP. */
esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles);
/** Release a panel created by bsp_display_new(). */
void bsp_display_delete(void);

#if !BSP_CONFIG_NO_GRAPHIC_LIB
/** Start LVGL with the default Candis configuration. Turn brightness on explicitly. */
lv_display_t *bsp_display_start(void);
/** Start the LVGL display with an explicit LVGL configuration. */
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *config);
/** Stop the display when the supplied handle belongs to the active instance. */
void bsp_display_stop(lv_display_t *display);
/** Return the LVGL input device for the active display. */
lv_indev_t *bsp_display_get_input_dev(void);
/** Rotate the active LVGL display in software. */
void bsp_display_rotate(lv_display_t *display, lv_display_rotation_t rotation);
/** Take the LVGL lock. A timeout of zero waits indefinitely. */
bool bsp_display_lock(uint32_t timeout_ms);
/** Release the LVGL lock. */
void bsp_display_unlock(void);
#endif

/** Put the panel and CST9220 into sleep without releasing their resources.
 * Repeated calls while already asleep are no-ops returning ESP_OK. */
esp_err_t bsp_display_enter_sleep(void);
/** Wake the panel and CST9220 after bsp_display_enter_sleep().
 * Repeated calls while already awake are no-ops returning ESP_OK. */
esp_err_t bsp_display_exit_sleep(void);

/**
 * @brief Initialize display brightness control.
 *
 * Candis uses panel register brightness and does not allocate a separate PWM
 * channel. The call is kept for ESP-BSP compatibility.
 */
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_deinit(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
