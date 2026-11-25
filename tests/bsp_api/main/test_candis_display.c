/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdlib.h>
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"

#define CAPI "candis"

static candis_display_config_t display_config(bool touch, bool manage_power)
{
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    config.brightness_percent = 20;
    config.enable_touch = touch;
    config.manage_panel_power = manage_power;
    config.async_flush = false;
    return config;
}

static void test_touch_standalone(void)
{
    esp_lcd_touch_handle_t touch = NULL;
    api_test_rail_state_t aldo1 = {0};
    const bool rail_captured = api_test_rail_capture(TG28_ESP_RAIL_ALDO1, &aldo1) == ESP_OK;
    const bool rail_ready = rail_captured &&
                            api_test_rail_force(TG28_ESP_RAIL_ALDO1, 3300, true) == ESP_OK;
    api_test_require(CAPI, "bsp_touch_new", API_ASPECT_BOUNDARY,
                     bsp_touch_new(NULL, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    esp_err_t err = rail_ready ? bsp_touch_new(NULL, &touch) : ESP_ERR_INVALID_STATE;
    if (rail_captured) {
        api_test_check_bool(CAPI, "bsp_touch_new", API_ASPECT_NORMAL, err == ESP_OK && touch != NULL,
                            "create CST9220 on the existing display supply");
    } else {
        api_test_check_bool(CAPI, "bsp_touch_new", API_ASPECT_ERROR,
                            err == ESP_ERR_INVALID_STATE, "display supply is required");
        API_BLOCKED(CAPI, "bsp_touch_new", API_ASPECT_HARDWARE,
                    "ALDO1 could not be captured and enabled safely");
    }
    if (err == ESP_OK && touch) {
        candis_touch_point_t points[2] = {0};
        uint8_t count = 0xff;
        const esp_err_t read = candis_touch_read(touch, points, &count);
        api_test_check_bool(CAPI, "candis_touch_read", API_ASPECT_NORMAL,
                            read == ESP_OK || read == ESP_ERR_NOT_FINISHED,
                            read == ESP_ERR_NOT_FINISHED ? "no report ready" : "report read");
        if (read == ESP_ERR_NOT_FINISHED) {
            API_BLOCKED(CAPI, "candis_touch_read", API_ASPECT_HARDWARE,
                        "no physical touch during automated run");
        } else {
            api_test_check_bool(CAPI, "candis_touch_read", API_ASPECT_HARDWARE, count <= 2,
                                "contact count within two-point capacity");
        }
        api_test_require(CAPI, "candis_touch_read", API_ASPECT_BOUNDARY,
                         candis_touch_read(NULL, points, &count), ESP_ERR_INVALID_ARG, "NULL handle");
        api_test_require(CAPI, "candis_touch_read", API_ASPECT_BOUNDARY,
                         candis_touch_read(touch, NULL, &count), ESP_ERR_INVALID_ARG, "NULL points");
        api_test_require(CAPI, "candis_touch_read", API_ASPECT_BOUNDARY,
                         candis_touch_read(touch, points, NULL), ESP_ERR_INVALID_ARG, "NULL count");
        api_test_require(CAPI, "bsp_touch_del", API_ASPECT_NORMAL,
                         bsp_touch_del(touch), ESP_OK, "release CST9220");
    }

    touch = NULL;
    api_test_require(CAPI, "candis_touch_new_with_config", API_ASPECT_BOUNDARY,
                     candis_touch_new_with_config(NULL, NULL), ESP_ERR_INVALID_ARG,
                     "NULL output");
    const candis_touch_config_t config = {
        .swap_xy = false,
        .mirror_x = false,
        .mirror_y = false,
        .manage_power = true,
    };
    err = candis_touch_new_with_config(&config, &touch);
    api_test_check_bool(CAPI, "candis_touch_new_with_config", API_ASPECT_NORMAL,
                        err == ESP_OK && touch != NULL, "explicit axis mapping");
    if (err == ESP_OK && touch) {
        api_test_require(CAPI, "bsp_touch_del", API_ASPECT_LIFECYCLE,
                         bsp_touch_del(touch), ESP_OK, "release explicit touch");
    }
    api_test_require(CAPI, "bsp_touch_del", API_ASPECT_LIFECYCLE,
                     bsp_touch_del(NULL), ESP_OK, "repeated/no-op cleanup");
    if (rail_ready) {
        api_test_check_bool("api-test", "restore_aldo1_touch", API_ASPECT_LIFECYCLE,
                            api_test_rail_restore(TG28_ESP_RAIL_ALDO1, &aldo1) == ESP_OK,
                            "restore ALDO1 after standalone touch tests");
    }
}

static void test_raw_display(void)
{
    uint16_t *frame = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame) {
        API_BLOCKED(CAPI, "candis_display_start_raw", API_ASPECT_HARDWARE,
                    "unable to allocate 480x480 PSRAM frame");
        return;
    }
    for (size_t i = 0; i < BSP_LCD_H_RES * BSP_LCD_V_RES; ++i) {
        const unsigned x = i % BSP_LCD_H_RES;
        const unsigned y = i / BSP_LCD_H_RES;
        frame[i] = (uint16_t)(((x >> 4) ^ (y >> 4)) ? 0x001f : 0x07e0);
    }
    candis_display_config_t config = display_config(false, true);
    api_test_require(CAPI, "candis_display_start_raw", API_ASPECT_BOUNDARY,
                     candis_display_start_raw(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    esp_err_t err = candis_display_start_raw(&config);
    api_test_require(CAPI, "candis_display_start_raw", API_ASPECT_NORMAL, err, ESP_OK,
                     "manage ALDO1 and create raw panel");
    if (err == ESP_OK) {
        candis_display_stats_t before = {0};
        candis_display_stats_t after = {0};
        api_test_require(CAPI, "candis_display_get_stats", API_ASPECT_BOUNDARY,
                         candis_display_get_stats(NULL), ESP_ERR_INVALID_ARG, "NULL stats");
        api_test_require(CAPI, "candis_display_get_stats", API_ASPECT_NORMAL,
                         candis_display_get_stats(&before), ESP_OK, "read stats before frame");
        api_test_require(CAPI, "candis_display_draw_rgb565", API_ASPECT_BOUNDARY,
                         candis_display_draw_rgb565(NULL), ESP_ERR_INVALID_ARG, "NULL frame");
        api_test_require(CAPI, "candis_display_draw_rgb565", API_ASPECT_NORMAL,
                         candis_display_draw_rgb565(frame), ESP_OK, "full RGB565 frame DMA completed");
        api_test_require(CAPI, "candis_display_get_stats", API_ASPECT_NORMAL,
                         candis_display_get_stats(&after), ESP_OK, "read stats after frame");
        api_test_check_bool(CAPI, "candis_display_get_stats", API_ASPECT_NORMAL,
                            after.frames > before.frames &&
                            after.pixels >= before.pixels + BSP_LCD_H_RES * BSP_LCD_V_RES,
                            "frame and pixel counters advanced");
        api_test_check_bool(CAPI, "candis_display_draw_rgb565", API_ASPECT_HARDWARE,
                            after.transfer_errors == before.transfer_errors,
                            "DMA frame completed without transfer errors");
        api_test_require(CAPI, "bsp_display_brightness_init", API_ASPECT_NORMAL,
                         bsp_display_brightness_init(), ESP_OK, "compatibility init");
        api_test_require(CAPI, "bsp_display_brightness_set", API_ASPECT_BOUNDARY,
                         bsp_display_brightness_set(-1), ESP_ERR_INVALID_ARG, "negative brightness");
        api_test_require(CAPI, "bsp_display_brightness_set", API_ASPECT_BOUNDARY,
                         bsp_display_brightness_set(101), ESP_ERR_INVALID_ARG, "brightness above 100");
        api_test_require(CAPI, "bsp_display_brightness_set", API_ASPECT_NORMAL,
                         bsp_display_brightness_set(20), ESP_OK, "set panel brightness");
        api_test_require(CAPI, "bsp_display_backlight_off", API_ASPECT_NORMAL,
                         bsp_display_backlight_off(), ESP_OK, "backlight off");
        api_test_require(CAPI, "bsp_display_backlight_on", API_ASPECT_NORMAL,
                         bsp_display_backlight_on(), ESP_OK, "backlight on");
        api_test_require(CAPI, "bsp_display_enter_sleep", API_ASPECT_NORMAL,
                         bsp_display_enter_sleep(), ESP_OK, "panel and touch sleep");
        api_test_require(CAPI, "bsp_display_enter_sleep", API_ASPECT_LIFECYCLE,
                         bsp_display_enter_sleep(), ESP_OK, "repeated sleep is idempotent");
        api_test_require(CAPI, "bsp_display_exit_sleep", API_ASPECT_NORMAL,
                         bsp_display_exit_sleep(), ESP_OK, "panel and touch wake");
        api_test_require(CAPI, "bsp_display_exit_sleep", API_ASPECT_LIFECYCLE,
                         bsp_display_exit_sleep(), ESP_OK, "repeated wake is idempotent");
        api_test_require(CAPI, "bsp_display_brightness_deinit", API_ASPECT_NORMAL,
                         bsp_display_brightness_deinit(), ESP_OK, "turn panel brightness off");
        api_test_require(CAPI, "candis_display_stop", API_ASPECT_LIFECYCLE,
                         candis_display_stop(), ESP_OK, "release raw display");
        api_test_require(CAPI, "candis_display_stop", API_ASPECT_LIFECYCLE,
                         candis_display_stop(), ESP_OK, "repeated release");
    } else {
        API_BLOCKED(CAPI, "candis_display_draw_rgb565", API_ASPECT_HARDWARE,
                    "raw display could not initialize");
    }
    heap_caps_free(frame);
}

static void test_panel_handles(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    api_test_rail_state_t aldo1 = {0};
    const bool rail_captured = api_test_rail_capture(TG28_ESP_RAIL_ALDO1, &aldo1) == ESP_OK;
    const bool rail_ready = rail_captured &&
                            api_test_rail_force(TG28_ESP_RAIL_ALDO1, 3300, true) == ESP_OK;
    api_test_require(CAPI, "bsp_display_new", API_ASPECT_BOUNDARY,
                     bsp_display_new(NULL, NULL, &io), ESP_ERR_INVALID_ARG, "NULL panel output");
    api_test_require(CAPI, "bsp_display_new", API_ASPECT_BOUNDARY,
                     bsp_display_new(NULL, &panel, NULL), ESP_ERR_INVALID_ARG, "NULL IO output");
    esp_err_t err = rail_ready ? bsp_display_new(NULL, &panel, &io) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && panel && io) {
        api_test_log(CAPI, "bsp_display_new", API_ASPECT_NORMAL, API_PASS,
                     "panel and IO handles returned");
        uint16_t pixels[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
        api_test_require(CAPI, "bsp_display_new", API_ASPECT_HARDWARE,
                         esp_lcd_panel_disp_on_off(panel, true), ESP_OK, "panel display on");
        api_test_require(CAPI, "bsp_display_new", API_ASPECT_HARDWARE,
                         esp_lcd_panel_draw_bitmap(panel, 0, 0, 2, 2, pixels), ESP_OK,
                         "2x2 DMA draw");
        bsp_display_delete();
        api_test_log(CAPI, "bsp_display_delete", API_ASPECT_NORMAL, API_PASS,
                     "panel release through BSP-owned handle");
    } else {
        API_BLOCKED(CAPI, "bsp_display_new", API_ASPECT_HARDWARE,
                    "ALDO1 not already powered for native panel creation");
    }

    bsp_lcd_handles_t handles = {0};
    api_test_require(CAPI, "bsp_display_new_with_handles", API_ASPECT_BOUNDARY,
                     bsp_display_new_with_handles(NULL, NULL), ESP_ERR_INVALID_ARG,
                     "NULL handles");
    err = rail_ready ? bsp_display_new_with_handles(NULL, &handles) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        api_test_check_bool(CAPI, "bsp_display_new_with_handles", API_ASPECT_NORMAL,
                            handles.panel && handles.io, "all owned handles returned");
        bsp_display_delete();
        api_test_log(CAPI, "bsp_display_delete", API_ASPECT_NORMAL, API_PASS,
                     "native panel resources released");
    } else {
        API_BLOCKED(CAPI, "bsp_display_new_with_handles", API_ASPECT_HARDWARE,
                    "ALDO1 not already powered for native panel creation");
    }
    if (rail_captured) {
        api_test_check_bool("api-test", "restore_aldo1", API_ASPECT_LIFECYCLE,
                            api_test_rail_restore(TG28_ESP_RAIL_ALDO1, &aldo1) == ESP_OK,
                            "restore ALDO1 after owned panel tests");
    } else {
        API_BLOCKED(CAPI, "bsp_display_delete", API_ASPECT_HARDWARE,
                    "ALDO1 could not be captured and enabled safely");
    }
}

static void test_lvgl_display(void)
{
    candis_display_config_t config = display_config(true, true);
    lv_display_t *display = NULL;
    esp_err_t err = candis_display_start(&config, &display);
    api_test_require(CAPI, "candis_display_start", API_ASPECT_BOUNDARY,
                     candis_display_start(&config, NULL), ESP_ERR_INVALID_ARG,
                     "NULL display output");
    if (err == ESP_OK && display) {
        api_test_log(CAPI, "candis_display_start", API_ASPECT_NORMAL, API_PASS,
                     "LVGL display created");
        api_test_check_bool(CAPI, "bsp_display_lock", API_ASPECT_NORMAL,
                            bsp_display_lock(100), "lock LVGL");
        candis_touch_point_t points[2] = {0};
        const uint8_t count = candis_display_get_touch_points(points);
        api_test_check_bool(CAPI, "candis_display_get_touch_points", API_ASPECT_NORMAL,
                            count <= 2, "snapshot returns at most two contacts");
        api_test_log(CAPI, "candis_display_get_touch_points", API_ASPECT_BOUNDARY, API_PASS,
                     "NULL points returns zero");
        api_test_check_bool(CAPI, "candis_display_get_touch_points", API_ASPECT_BOUNDARY,
                            candis_display_get_touch_points(NULL) == 0, "NULL points returns zero");
        bsp_display_unlock();
        lv_indev_t *input = bsp_display_get_input_dev();
        api_test_check_bool(CAPI, "bsp_display_get_input_dev", API_ASPECT_NORMAL,
                            input != NULL, "touch input device available");
        bsp_display_rotate(display, LV_DISPLAY_ROTATION_90);
        bsp_display_rotate(display, LV_DISPLAY_ROTATION_0);
        api_test_log(CAPI, "bsp_display_rotate", API_ASPECT_NORMAL, API_PASS,
                     "rotated to 90 and restored to 0");
        api_test_require(CAPI, "bsp_display_brightness_set", API_ASPECT_NORMAL,
                         bsp_display_brightness_set(30), ESP_OK, "brightness while active");
        candis_display_stats_t stats;
        api_test_require(CAPI, "candis_display_get_stats", API_ASPECT_NORMAL,
                         candis_display_get_stats(&stats), ESP_OK, "LVGL stats path");
        bsp_display_stop(display);
        api_test_log(CAPI, "bsp_display_stop", API_ASPECT_NORMAL, API_PASS,
                     "active LVGL display stopped");
        bsp_display_stop(display);
        api_test_log(CAPI, "bsp_display_stop", API_ASPECT_LIFECYCLE, API_PASS,
                     "stale handle stop is a safe no-op");
        api_test_require(CAPI, "candis_display_stop", API_ASPECT_LIFECYCLE,
                         candis_display_stop(), ESP_OK, "cleanup");
    } else {
        API_BLOCKED(CAPI, "candis_display_start", API_ASPECT_HARDWARE,
                    "LVGL display could not initialize");
    }

    candis_display_config_t defaults = CANDIS_DISPLAY_CONFIG_DEFAULT();
    defaults.brightness_percent = 0;
    lv_display_t *diagnostic = NULL;
    esp_err_t default_err = candis_display_start(&defaults, &diagnostic);
    api_test_require(CAPI, "candis_display_start", API_ASPECT_LIFECYCLE,
                     default_err, ESP_OK, "restart with default touch and async settings");
    if (default_err == ESP_OK) {
        bsp_display_stop(diagnostic);
    }

    lv_display_t *started = bsp_display_start();
    api_test_check_bool(CAPI, "bsp_display_start", API_ASPECT_NORMAL, started != NULL,
                        "default LVGL configuration");
    if (started) {
        bsp_display_stop(started);
    }
    bsp_display_unlock();
    api_test_log(CAPI, "bsp_display_unlock", API_ASPECT_LIFECYCLE, API_PASS,
                 "unmatched unlock is safe");
    api_test_check_bool(CAPI, "bsp_display_lock", API_ASPECT_ERROR,
                        !bsp_display_lock(1), "lock fails while display stopped");
    api_test_check_bool(CAPI, "bsp_display_get_input_dev", API_ASPECT_ERROR,
                        bsp_display_get_input_dev() == NULL, "input device unavailable while stopped");

    bsp_display_cfg_t ui = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG()};
    ui.lvgl_port_cfg.task_stack = 6144;
    ui.lvgl_port_cfg.task_affinity = 0;
    started = bsp_display_start_with_config(&ui);
    api_test_check_bool(CAPI, "bsp_display_start_with_config", API_ASPECT_NORMAL,
                        started != NULL, "explicit LVGL configuration");
    if (started) {
        bsp_display_stop(started);
    }
}

void api_tests_candis_display(void)
{
    test_panel_handles();
    test_raw_display();
    test_lvgl_display();
    test_touch_standalone();
}
