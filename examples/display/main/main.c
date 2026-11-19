/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "candis_example";
static lv_obj_t *s_view;
static lv_obj_t *s_tiles[16];
static lv_obj_t *s_markers[2];
static lv_timer_t *s_animation;
static lv_timer_t *s_contacts;
static bool s_full;
static unsigned s_step;

static void animate(lv_timer_t *timer)
{
    (void)timer;
    uint32_t color = (s_step++ % 120) * 0x010100 + 0x284030;
    lv_obj_set_style_bg_color(s_view, lv_color_hex(color), 0);
    const int width = s_full ? 480 : 256;
    for (int i = 0; i < 16; ++i) {
        int offset = (s_step * 3 + i * 29) % (width - 22);
        lv_obj_set_pos(s_tiles[i], offset, (i * 11) % 172);
    }
}

static void select_mode(bool full)
{
    s_full = full;
    lv_obj_set_size(s_view, full ? 480 : 256, full ? 480 : 192);
    lv_obj_center(s_view);
    lv_obj_invalidate(s_view);
}

static void change_mode(lv_event_t *event)
{
    select_mode(lv_event_get_user_data(event) != NULL);
}

static void show_contacts(lv_timer_t *timer)
{
    (void)timer;
    candis_touch_point_t points[2];
    unsigned count = candis_display_get_touch_points(points);
    for (unsigned i = 0; i < 2; ++i) {
        if (i < count) {
            lv_obj_set_hidden(s_markers[i], false);
            lv_obj_set_pos(s_markers[i], points[i].x - 9, points[i].y - 9);
        } else {
            lv_obj_set_hidden(s_markers[i], true);
        }
    }
}

static void make_button(lv_obj_t *parent, const char *text, int x, bool full)
{
    static const bool full_mode = true;
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 105, 42);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, 10);
    lv_obj_add_event_cb(button, change_mode, LV_EVENT_CLICKED, full ? (void *)&full_mode : NULL);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static esp_err_t setup_ui(lv_display_t *display)
{
    if (!bsp_display_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_scrollable(screen, false);
    s_view = lv_obj_create(screen);
    lv_obj_remove_style_all(s_view);
    lv_obj_set_scrollable(s_view, false);
    lv_obj_set_clickable(s_view, false);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    for (int i = 0; i < 16; ++i) {
        s_tiles[i] = lv_obj_create(s_view);
        lv_obj_remove_style_all(s_tiles[i]);
        lv_obj_set_clickable(s_tiles[i], false);
        lv_obj_set_size(s_tiles[i], 22, 18);
        lv_obj_set_style_bg_opa(s_tiles[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(i % 2 ? 0xf5bd65 : 0x60d7cf), 0);
    }
    select_mode(false);
    make_button(screen, "Partial", 10, false);
    make_button(screen, "Full", 126, true);
    for (unsigned i = 0; i < 2; ++i) {
        s_markers[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(s_markers[i]);
        lv_obj_set_size(s_markers[i], 18, 18);
        lv_obj_set_style_radius(s_markers[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_markers[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_markers[i], lv_color_hex(i ? 0xff7070 : 0xffffff), 0);
        lv_obj_set_clickable(s_markers[i], false);
        lv_obj_set_hidden(s_markers[i], true);
    }
    s_animation = lv_timer_create(animate, 5, NULL);
    s_contacts = lv_timer_create(show_contacts, 10, NULL);
    bsp_display_unlock();
    return s_animation && s_contacts ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t sample(const char *label, unsigned duration_ms)
{
    candis_display_stats_t before, after;
    esp_err_t err = candis_display_get_stats(&before);
    if (err != ESP_OK) {
        return err;
    }
    const int64_t start_us = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    err = candis_display_get_stats(&after);
    if (err != ESP_OK) {
        return err;
    }
    const int64_t elapsed = esp_timer_get_time() - start_us;
    const uint64_t frames = after.frames - before.frames;
    const uint64_t pixels = after.pixels - before.pixels;
    ESP_LOGI(TAG, "%s frames=%" PRIu64 " fps=%.2f te_hz=%.2f mean_pixels=%" PRIu64
             " prepare_us=%" PRIu64 " mean_transfer_us=%" PRIu64 " te_wait_us=%" PRIu64
             " te_timeouts=%" PRIu32 " transfer_errors=%" PRIu32 " touch_errors=%" PRIu32,
             label, frames, frames * 1000000.0 / elapsed,
             (after.te_edges - before.te_edges) * 1000000.0 / elapsed,
             frames ? pixels / frames : 0, frames ? (after.prepare_us - before.prepare_us) / frames : 0,
             frames ? (after.transfer_us - before.transfer_us) / frames : 0,
             frames ? (after.te_wait_us - before.te_wait_us) / frames : 0,
             after.te_timeouts - before.te_timeouts, after.transfer_errors - before.transfer_errors,
             after.touch_errors - before.touch_errors);
    return ESP_OK;
}

void app_main(void)
{
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_BENCH_PANEL_POWER
    config.manage_panel_power = true;
#endif
#ifdef CONFIG_CANDIS_BENCH_QSPI_80MHZ
    config.bench_qspi_80mhz = true;
#endif
    lv_display_t *display;
    ESP_ERROR_CHECK(candis_display_start(&config, &display));
    ESP_LOGI(TAG, "display started at %d MHz", config.bench_qspi_80mhz ? 80 : 50);
    ESP_ERROR_CHECK(setup_ui(display));
#ifdef CONFIG_CANDIS_BENCH_AUTORUN
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(sample("partial", 8000));
    if (!bsp_display_lock(2000)) {
        abort();
    }
    select_mode(true);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(sample("full", 8000));
    if (!bsp_display_lock(2000)) {
        abort();
    }
    lv_timer_delete(s_animation);
    lv_timer_delete(s_contacts);
    bsp_display_unlock();
    ESP_ERROR_CHECK(candis_display_stop());
    ESP_LOGI(TAG, "display stopped");
    ESP_ERROR_CHECK(candis_display_start(&config, &display));
    ESP_ERROR_CHECK(setup_ui(display));
    ESP_LOGI(TAG, "display restarted");
#endif
    while (true) {
        ESP_ERROR_CHECK(sample("live", 10000));
    }
}
