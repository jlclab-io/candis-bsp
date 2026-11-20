/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include "bsp/esp-bsp.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "dock_display";
enum { BAR_WIDTH = 32, REPORT_US = 5000000 };

static lv_obj_t *s_surface;
static lv_obj_t *s_bar;
static lv_obj_t *s_marker[2];
static lv_timer_t *s_animation;
static lv_indev_read_cb_t s_touch_read;
static bool s_needs_stop;
static bool s_full;
static bool s_touch_enabled;
static unsigned s_step;
static uint32_t s_touch_polls;
static uint32_t s_contact_polls;
static int64_t s_last_touch_us;
static uint32_t s_max_gap_us;
static int64_t s_report_at;
static candis_display_stats_t s_previous;

static void animate(lv_timer_t *timer)
{
    const unsigned travel = BSP_LCD_H_RES - BAR_WIDTH;
    const unsigned phase = (s_step++ * 4) % (2 * travel);
    lv_obj_set_x(s_bar, phase < travel ? phase : 2 * travel - phase);
    if (s_full) {
        lv_obj_invalidate(s_surface);
    }
}

static void observe_touch(lv_indev_t *input, lv_indev_data_t *data)
{
    const int64_t now = esp_timer_get_time();
    if (s_last_touch_us && now - s_last_touch_us > s_max_gap_us) {
        s_max_gap_us = now - s_last_touch_us;
    }
    s_last_touch_us = now;
    ++s_touch_polls;
    s_touch_read(input, data);
    candis_touch_point_t points[2];
    const unsigned count = candis_display_get_touch_points(points);
    s_contact_polls += count != 0;
    for (unsigned i = 0; i < 2; ++i) {
        lv_obj_set_hidden(s_marker[i], i >= count);
        if (i < count) {
            lv_obj_set_pos(s_marker[i], points[i].x - 10, points[i].y - 10);
        }
    }
}

static lv_obj_t *make_shape(lv_obj_t *parent, int width, int height, uint32_t rgb)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_remove_flag(shape, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_color(shape, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    return shape;
}

static esp_err_t setup_display(lv_display_t *display, bool touch)
{
    lv_indev_t *input = lv_indev_get_next(NULL);
    while (input && (lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER || lv_indev_get_display(input) != display)) {
        input = lv_indev_get_next(input);
    }
    s_touch_read = input ? lv_indev_get_read_cb(input) : NULL;
    if (touch && !s_touch_read) {
        return ESP_ERR_NOT_FOUND;
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_surface = make_shape(screen, BSP_LCD_H_RES, BSP_LCD_V_RES, 0x090c14);
    lv_obj_t *label = lv_label_create(s_surface);
    lv_obj_set_style_text_color(label, lv_color_hex(0xe2e8f0), 0);
    lv_label_set_text(label, touch ? "Candis Dock\nDrag to check touch" : "Candis Dock\nPanel check");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 42);
    s_bar = make_shape(s_surface, BAR_WIDTH, 112, 0x4bd1d3);
    lv_obj_set_pos(s_bar, 0, (BSP_LCD_V_RES - 112) / 2);
    for (unsigned i = 0; i < 2; ++i) {
        s_marker[i] = make_shape(s_surface, 20, 20, i ? 0xffbe69 : 0xfc709a);
        lv_obj_set_style_radius(s_marker[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_hidden(s_marker[i], true);
    }
    s_full = false;
    s_touch_enabled = touch;
    s_step = 0;
    s_touch_polls = 0;
    s_contact_polls = 0;
    s_last_touch_us = 0;
    s_max_gap_us = 0;
    s_animation = lv_timer_create(animate, 10, NULL);
    if (!s_animation) {
        return ESP_ERR_NO_MEM;
    }
    if (touch) {
        lv_indev_set_read_cb(input, observe_touch);
    }
    return ESP_OK;
}

esp_err_t dock_display_start(bool touch)
{
    if (s_needs_stop) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    config.manage_panel_power = true;
    config.enable_touch = touch;
    config.brightness_percent = 35;
    lv_display_t *display;
    s_needs_stop = true;
    esp_err_t err = candis_display_start(&config, &display);
    if (err == ESP_OK) {
        if (bsp_display_lock(250)) {
            err = setup_display(display, touch);
            bsp_display_unlock();
        } else {
            err = ESP_ERR_TIMEOUT;
        }
    }
    if (err == ESP_OK) {
        err = dock_display_mode(false);
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(dock_display_stop());
    }
    return err;
}

esp_err_t dock_display_stop(void)
{
    if (!s_needs_stop) {
        return ESP_OK;
    }
    if (s_animation) {
        if (!bsp_display_lock(250)) {
            return ESP_ERR_TIMEOUT;
        }
        lv_timer_delete(s_animation);
        s_animation = NULL;
        bsp_display_unlock();
    }
    const esp_err_t err = candis_display_stop();
    s_needs_stop = err != ESP_OK;
    s_report_at = 0;
    return err;
}

esp_err_t dock_display_mode(bool full)
{
    if (!s_animation) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!bsp_display_lock(250)) {
        return ESP_ERR_TIMEOUT;
    }
    s_full = full;
    s_touch_polls = 0;
    s_contact_polls = 0;
    s_max_gap_us = 0;
    const esp_err_t err = candis_display_get_stats(&s_previous);
    s_report_at = err == ESP_OK ? esp_timer_get_time() + REPORT_US : 0;
    bsp_display_unlock();
    return err;
}

void dock_display_report(void)
{
    const int64_t now = esp_timer_get_time();
    if (!s_report_at || now < s_report_at || !bsp_display_lock(30)) {
        return;
    }
    candis_display_stats_t stats;
    const esp_err_t err = candis_display_get_stats(&stats);
    const uint32_t polls = s_touch_polls;
    const uint32_t contacts = s_contact_polls;
    const uint32_t gap = s_max_gap_us;
    s_touch_polls = 0;
    s_contact_polls = 0;
    s_max_gap_us = 0;
    bsp_display_unlock();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display stats: %s", esp_err_to_name(err));
        return;
    }
    const int64_t elapsed = now - (s_report_at - REPORT_US);
    const uint64_t frames = stats.frames - s_previous.frames;
    const uint64_t pixels = stats.pixels - s_previous.pixels;
    ESP_LOGI(TAG, "DISPLAY %s touch=%d fps=%.2f te_hz=%.2f pixels_per_frame=%" PRIu64
             " te_timeouts=%" PRIu32 " transfer_errors=%" PRIu32 " touch_errors=%" PRIu32
             " touch_polls=%" PRIu32 " contact_polls=%" PRIu32 " max_touch_gap_us=%" PRIu32,
             s_full ? "full" : "partial", s_touch_enabled, 1000000.0 * frames / elapsed,
             (stats.te_edges - s_previous.te_edges) * 1000000.0 / elapsed, frames ? pixels / frames : 0,
             stats.te_timeouts - s_previous.te_timeouts, stats.transfer_errors - s_previous.transfer_errors,
             stats.touch_errors - s_previous.touch_errors, polls, contacts, gap);
    s_previous = stats;
    s_report_at = now + REPORT_US;
}
