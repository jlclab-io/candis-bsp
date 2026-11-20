/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <inttypes.h>
#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SCAN_BAR_WIDTH 24
#define SCAN_BAR_TOP 56
#define TOUCH_MARKER_COUNT 2

static const char *TAG = "candis_example";
typedef enum {
    DISPLAY_PARTIAL,
    DISPLAY_FULL,
    DISPLAY_SCAN,
} display_mode_t;

static const char *const s_mode_names[] = {"partial", "full", "scan"};

typedef struct {
    lv_obj_t *object;
    uint8_t track_id;
    bool active;
} touch_marker_t;

static lv_obj_t *s_view;
static lv_obj_t *s_tiles[16];
static lv_obj_t *s_scan_bar;
static touch_marker_t s_markers[TOUCH_MARKER_COUNT];
static lv_timer_t *s_animation;
static lv_indev_read_cb_t s_read_touch;
static int64_t s_last_touch_poll_us;
static uint32_t s_touch_poll_gap_max_us;
static uint32_t s_touch_polls[TOUCH_MARKER_COUNT + 1];
static display_mode_t s_mode;
static unsigned s_step;

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
static void read_idle_time(configRUN_TIME_COUNTER_TYPE idle[configNUMBER_OF_CORES])
{
    for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
        idle[core] = ulTaskGetIdleRunTimeCounterForCore(core);
    }
}
#endif

static void animate(lv_timer_t *timer)
{
    (void)timer;
    if (s_mode == DISPLAY_SCAN) {
        const unsigned travel = BSP_LCD_H_RES - SCAN_BAR_WIDTH;
        const unsigned phase = (s_step++ * 5) % (2 * travel);
        lv_obj_set_x(s_scan_bar, phase < travel ? phase : 2 * travel - phase);
        return;
    }
    uint32_t color = (s_step++ % 120) * 0x010100 + 0x284030;
    lv_obj_set_style_bg_color(s_view, lv_color_hex(color), 0);
    const int width = s_mode == DISPLAY_FULL ? BSP_LCD_H_RES : 256;
    for (int i = 0; i < 16; ++i) {
        int offset = (s_step * 3 + i * 29) % (width - 22);
        lv_obj_set_pos(s_tiles[i], offset, (i * 11) % 172);
    }
}

static void select_mode(display_mode_t mode)
{
    s_mode = mode;
    const bool scan = mode == DISPLAY_SCAN;
    const bool partial = mode == DISPLAY_PARTIAL;
    lv_obj_set_size(s_view, partial ? 256 : BSP_LCD_H_RES, partial ? 192 : BSP_LCD_V_RES);
    lv_obj_center(s_view);
    for (size_t i = 0; i < sizeof(s_tiles) / sizeof(s_tiles[0]); ++i) {
        lv_obj_set_hidden(s_tiles[i], scan);
    }
    lv_obj_set_hidden(s_scan_bar, !scan);
    if (scan) {
        lv_obj_set_style_bg_color(s_view, lv_color_black(), 0);
        s_step = 0;
        lv_obj_set_x(s_scan_bar, 0);
    }
    lv_obj_invalidate(s_view);
}

static void change_mode(lv_event_t *event)
{
    select_mode((display_mode_t)(uintptr_t)lv_event_get_user_data(event));
}

static void update_markers(const candis_touch_point_t points[2], unsigned count)
{
    bool matched[TOUCH_MARKER_COUNT] = {false};
    int point_index[TOUCH_MARKER_COUNT] = {-1, -1};
    /* Keep the same marker when controller report order changes or another finger lifts. */
    for (unsigned i = 0; i < TOUCH_MARKER_COUNT; ++i) {
        if (s_markers[i].active) {
            for (unsigned j = 0; j < count; ++j) {
                if (!matched[j] && points[j].track_id == s_markers[i].track_id) {
                    point_index[i] = j;
                    matched[j] = true;
                    break;
                }
            }
        }
    }
    for (unsigned i = 0; i < TOUCH_MARKER_COUNT; ++i) {
        touch_marker_t *marker = &s_markers[i];
        if (point_index[i] < 0) {
            for (unsigned j = 0; j < count; ++j) {
                if (!matched[j]) {
                    point_index[i] = j;
                    matched[j] = true;
                    break;
                }
            }
        }
        const bool active = point_index[i] >= 0;
        if (active) {
            const candis_touch_point_t *point = &points[point_index[i]];
            marker->track_id = point->track_id;
            lv_obj_set_pos(marker->object, point->x - 9, point->y - 9);
        }
        if (active != marker->active) {
            lv_obj_set_hidden(marker->object, !active);
            marker->active = active;
        }
    }
}

static void read_and_show_contacts(lv_indev_t *input, lv_indev_data_t *data)
{
    const int64_t now_us = esp_timer_get_time();
    if (s_last_touch_poll_us) {
        const int64_t gap_us = now_us - s_last_touch_poll_us;
        if (gap_us > s_touch_poll_gap_max_us) {
            s_touch_poll_gap_max_us = gap_us > UINT32_MAX ? UINT32_MAX : (uint32_t)gap_us;
        }
    }
    s_last_touch_poll_us = now_us;
    s_read_touch(input, data);
    candis_touch_point_t points[TOUCH_MARKER_COUNT];
    const unsigned count = candis_display_get_touch_points(points);
    ++s_touch_polls[count];
    update_markers(points, count);
}

static void make_button(lv_obj_t *parent, const char *text, int x, display_mode_t mode)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 105, 42);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, x, 10);
    lv_obj_add_event_cb(button, change_mode, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static esp_err_t setup_ui(lv_display_t *display)
{
    if (!bsp_display_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_indev_t *input = lv_indev_get_next(NULL);
    while (input && (lv_indev_get_display(input) != display || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER)) {
        input = lv_indev_get_next(input);
    }
    s_read_touch = input ? lv_indev_get_read_cb(input) : NULL;
    if (!s_read_touch) {
        bsp_display_unlock();
        return ESP_ERR_NOT_FOUND;
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
    s_scan_bar = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_scan_bar);
    lv_obj_set_size(s_scan_bar, SCAN_BAR_WIDTH, BSP_LCD_V_RES - SCAN_BAR_TOP);
    lv_obj_set_y(s_scan_bar, SCAN_BAR_TOP);
    lv_obj_set_style_bg_opa(s_scan_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scan_bar, lv_color_hex(0x60d7cf), 0);
    lv_obj_set_clickable(s_scan_bar, false);
    lv_obj_set_scrollable(s_scan_bar, false);
    select_mode(DISPLAY_PARTIAL);
    make_button(screen, "Partial", 10, DISPLAY_PARTIAL);
    make_button(screen, "Full", 126, DISPLAY_FULL);
    make_button(screen, "Scan", 242, DISPLAY_SCAN);
    for (unsigned i = 0; i < TOUCH_MARKER_COUNT; ++i) {
        lv_obj_t *marker = lv_obj_create(screen);
        s_markers[i] = (touch_marker_t){.object = marker};
        lv_obj_remove_style_all(marker);
        lv_obj_set_size(marker, 18, 18);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(i ? 0xff7070 : 0xffffff), 0);
        lv_obj_set_clickable(marker, false);
        lv_obj_set_hidden(marker, true);
    }
    s_animation = lv_timer_create(animate, 5, NULL);
    s_last_touch_poll_us = 0;
    s_touch_poll_gap_max_us = 0;
    lv_indev_set_read_cb(input, read_and_show_contacts);
    bsp_display_unlock();
    return s_animation ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t sample(const char *label, unsigned duration_ms)
{
    candis_display_stats_t before, after;
    esp_err_t err = candis_display_get_stats(&before);
    if (err != ESP_OK) {
        return err;
    }
    if (!bsp_display_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
    s_touch_poll_gap_max_us = 0;
    s_touch_polls[0] = s_touch_polls[1] = s_touch_polls[2] = 0;
    const display_mode_t mode = s_mode;
    bsp_display_unlock();
    const int64_t start_us = esp_timer_get_time();
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
    configRUN_TIME_COUNTER_TYPE idle_before[configNUMBER_OF_CORES], idle_after[configNUMBER_OF_CORES];
    read_idle_time(idle_before);
#endif
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    err = candis_display_get_stats(&after);
    if (err != ESP_OK) {
        return err;
    }
    if (!bsp_display_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
    const uint32_t touch_gap_us = s_touch_poll_gap_max_us;
    const uint32_t zero_touch_polls = s_touch_polls[0];
    const uint32_t one_touch_polls = s_touch_polls[1];
    const uint32_t two_touch_polls = s_touch_polls[2];
    const display_mode_t end_mode = s_mode;
    bsp_display_unlock();
    const int64_t elapsed = esp_timer_get_time() - start_us;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
    read_idle_time(idle_after);
#endif
    const uint64_t frames = after.frames - before.frames;
    const uint64_t pixels = after.pixels - before.pixels;
    ESP_LOGI(TAG, "%s frames=%" PRIu64 " fps=%.2f te_hz=%.2f mean_pixels=%" PRIu64
             " prepare_us=%" PRIu64 " mean_transfer_us=%" PRIu64 " te_wait_us=%" PRIu64
             " te_timeouts=%" PRIu32 " transfer_errors=%" PRIu32 " touch_errors=%" PRIu32
             " touch_poll_gap_max_us=%" PRIu32,
             label, frames, frames * 1000000.0 / elapsed,
             (after.te_edges - before.te_edges) * 1000000.0 / elapsed,
             frames ? pixels / frames : 0, frames ? (after.prepare_us - before.prepare_us) / frames : 0,
             frames ? (after.transfer_us - before.transfer_us) / frames : 0,
             frames ? (after.te_wait_us - before.te_wait_us) / frames : 0,
             after.te_timeouts - before.te_timeouts, after.transfer_errors - before.transfer_errors,
             after.touch_errors - before.touch_errors, touch_gap_us);
    if (one_touch_polls || two_touch_polls || mode != end_mode || mode == DISPLAY_SCAN) {
        ESP_LOGI(TAG, "interaction start_mode=%s end_mode=%s zero_touch_polls=%" PRIu32
                 " one_touch_polls=%" PRIu32 " two_touch_polls=%" PRIu32,
                 s_mode_names[mode], s_mode_names[end_mode], zero_touch_polls,
                 one_touch_polls, two_touch_polls);
    }
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER && !CONFIG_FREERTOS_SMP
    for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
        const configRUN_TIME_COUNTER_TYPE idle_us = idle_after[core] - idle_before[core];
        ESP_LOGI(TAG, "%s cpu%d_busy_pct=%.1f", label, (int)core,
                 100.0 * (1.0 - (double)LV_MIN(idle_us, (uint64_t)elapsed) / elapsed));
    }
#endif
    return ESP_OK;
}

#ifdef CONFIG_CANDIS_BENCH_AUTORUN
static void log_heap(const char *phase, unsigned round)
{
    ESP_LOGI(TAG, "heap %s round=%u free_internal=%zu largest_internal=%zu free_psram=%zu largest_psram=%zu",
             phase, round, heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_BENCH_PANEL_POWER
    config.manage_panel_power = true;
#endif
#ifdef CONFIG_CANDIS_BENCH_QSPI_50MHZ
    config.bench_qspi_80mhz = false;
#endif
#ifdef CONFIG_CANDIS_BENCH_SYNC_FLUSH
    config.async_flush = false;
#endif
    lv_display_t *display;
    ESP_ERROR_CHECK(candis_display_start(&config, &display));
    ESP_LOGI(TAG, "display started at %d MHz, async_flush=%d", config.bench_qspi_80mhz ? 80 : 50,
             config.async_flush);
    ESP_ERROR_CHECK(setup_ui(display));
#ifdef CONFIG_CANDIS_BENCH_AUTORUN
    for (unsigned round = 1; round <= CONFIG_CANDIS_BENCH_ROUNDS; ++round) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        log_heap("active", round);
        ESP_ERROR_CHECK(sample("partial", 8000));
        for (display_mode_t mode = DISPLAY_FULL; mode <= DISPLAY_SCAN; ++mode) {
            if (!bsp_display_lock(2000)) {
                abort();
            }
            select_mode(mode);
            bsp_display_unlock();
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_ERROR_CHECK(sample(s_mode_names[mode], 8000));
        }
        if (!bsp_display_lock(2000)) {
            abort();
        }
        lv_timer_delete(s_animation);
        bsp_display_unlock();
        ESP_ERROR_CHECK(candis_display_stop());
        vTaskDelay(pdMS_TO_TICKS(100));
        log_heap("stopped", round);
        ESP_LOGI(TAG, "display stopped");
        ESP_ERROR_CHECK(candis_display_start(&config, &display));
        ESP_ERROR_CHECK(setup_ui(display));
        ESP_LOGI(TAG, "display restarted");
    }
#endif
    while (true) {
        ESP_ERROR_CHECK(sample("live", 10000));
    }
}
