/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <stdio.h>
#include <string.h>
#include "bsp/candis.h"
#include "esp_check.h"
#include "recorder.h"
#include "sdkconfig.h"

static const char *TAG = "candis_recorder";
static struct {
    lv_obj_t *status;
    lv_obj_t *duration;
    lv_obj_t *meter[CANDIS_AUDIO_CHANNELS];
    lv_obj_t *volume_value;
    lv_obj_t *route;
    lv_obj_t *record;
    lv_obj_t *play;
    lv_obj_t *stop;
    uint32_t request;
    bool stopping;
} s_ui;

static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

static void enable_button(lv_obj_t *button, bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
}

static void buttons(bool busy, bool clip)
{
    enable_button(s_ui.record, !busy);
    enable_button(s_ui.play, !busy && clip);
    enable_button(s_ui.stop, busy && !s_ui.stopping);
    enable_button(s_ui.route, !busy);
}

static void start_clicked(lv_event_t *event)
{
    recorder_status_t status;
    if (!recorder_status(&status) || status.request != s_ui.request || status.mode != RECORDER_IDLE) {
        return;
    }
    const bool record = lv_event_get_target_obj(event) == s_ui.record;
    const recorder_route_t route = lv_dropdown_get_selected(s_ui.route);
    const uint32_t request = s_ui.request + 1;
    if (recorder_start(record ? RECORDER_RECORD : RECORDER_PLAY, route, request)) {
        s_ui.request = request;
        s_ui.stopping = false;
        buttons(true, status.clip_frames > 0);
        set_label(s_ui.status, record ? "Opening microphones" : "Opening playback");
    }
}

static void stop_clicked(lv_event_t *event)
{
    (void)event;
    s_ui.stopping = true;
    recorder_stop();
    enable_button(s_ui.stop, false);
    set_label(s_ui.status, "Stopping");
}

static void volume_changed(lv_event_t *event)
{
    const unsigned volume = lv_slider_get_value(lv_event_get_target_obj(event));
    recorder_set_volume(volume);
    lv_label_set_text_fmt(s_ui.volume_value, "%u%%", volume);
}

static void update_ui(lv_timer_t *timer)
{
    (void)timer;
    recorder_status_t status;
    if (!recorder_status(&status) || status.request != s_ui.request) {
        return;
    }
    const bool busy = status.mode != RECORDER_IDLE;
    const bool clip = status.clip_frames != 0;
    const bool playback = status.mode == RECORDER_PLAY;
    const size_t frames = busy ? status.progress : status.clip_frames;
    const unsigned tenths = (unsigned)(frames / (CANDIS_AUDIO_RATE / 10));
    char time[24];
    snprintf(time, sizeof(time), "%u.%u s", tenths / 10, tenths % 10);
    set_label(s_ui.duration, time);

    if (busy) {
        if (s_ui.stopping) {
            set_label(s_ui.status, "Stopping");
        } else if (status.active) {
            set_label(s_ui.status, playback ? "Playing" : "Recording");
        } else {
            set_label(s_ui.status, playback ? "Opening playback" : "Opening microphones");
        }
    } else if (status.cleanup != ESP_OK) {
        set_label(s_ui.status, "Audio release failed");
    } else if (status.result != ESP_OK) {
        set_label(s_ui.status, esp_err_to_name(status.result));
    } else if (status.clipped_channels) {
        const char *clipping = status.clipped_channels == 3 ? "L and R clipped" :
                               (status.clipped_channels == 1 ? "L clipped" : "R clipped");
        set_label(s_ui.status, clipping);
    } else {
        set_label(s_ui.status, clip ? "Clip ready" : "Ready to record");
    }
    for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
        const int level = status.peak[ch] * 100 / 32768U;
        if (lv_bar_get_value(s_ui.meter[ch]) != level) {
            lv_bar_set_value(s_ui.meter[ch], level, LV_ANIM_OFF);
        }
    }
    buttons(busy, clip);
}

static lv_obj_t *label_at(lv_obj_t *parent, const char *text, const lv_font_t *font, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xe4eaf3), 0);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *action(lv_obj_t *parent, const char *caption, int x, uint32_t color, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 128, 64);
    lv_obj_set_pos(button, x, 384);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x242c3b), LV_STATE_DISABLED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, caption);
    lv_obj_center(label);
    return button;
}

static void create_ui(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111722), 0);
    label_at(screen, "Audio recorder", &lv_font_montserrat_32, 32, 27);
    s_ui.status = label_at(screen, "Ready to record", &lv_font_montserrat_20, 32, 87);
    s_ui.duration = label_at(screen, "0.0 s", &lv_font_montserrat_32, 337, 78);
    for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
        const int y = 148 + 39 * ch;
        label_at(screen, ch == 0 ? "L" : "R", &lv_font_montserrat_20, 32, y - 3);
        lv_obj_t *bar = lv_bar_create(screen);
        lv_obj_set_size(bar, 384, 19);
        lv_obj_set_pos(bar, 62, y);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x2a3344), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(ch == 0 ? 0x83ddbd : 0x8bb8f7), LV_PART_INDICATOR);
        s_ui.meter[ch] = bar;
    }
    label_at(screen, "Playback volume", &lv_font_montserrat_20, 32, 234);
    s_ui.volume_value = label_at(screen, "", &lv_font_montserrat_20, 376, 234);
    lv_obj_set_width(s_ui.volume_value, 72);
    lv_obj_set_style_text_align(s_ui.volume_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(s_ui.volume_value, "%u%%", (unsigned)RECORDER_DEFAULT_VOLUME);
    lv_obj_t *volume = lv_slider_create(screen);
    lv_obj_set_size(volume, 392, 16);
    lv_obj_set_pos(volume, 44, 275);
    lv_slider_set_range(volume, 0, RECORDER_MAX_VOLUME);
    lv_slider_set_value(volume, RECORDER_DEFAULT_VOLUME, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(volume, 12);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0x2a3344), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0x8bb8f7), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume, lv_color_hex(0xe4eaf3), LV_PART_KNOB);
    lv_obj_add_event_cb(volume, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    label_at(screen, "Playback channel", &lv_font_montserrat_20, 32, 325);
    s_ui.route = lv_dropdown_create(screen);
    lv_dropdown_set_options_static(s_ui.route, "Stereo\nL only\nR only");
    lv_obj_set_pos(s_ui.route, 253, 310);
    lv_obj_set_size(s_ui.route, 195, 54);
    s_ui.record = action(screen, "Record", 32, 0xab3f54, start_clicked);
    s_ui.play = action(screen, "Play", 176, 0x35629a, start_clicked);
    s_ui.stop = action(screen, "Stop", 320, 0x5f4b75, stop_clicked);
    buttons(false, false);
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    esp_err_t err = recorder_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Allocate clip and queues: %s", esp_err_to_name(err));
        return;
    }
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_RECORDER_MANAGE_PANEL_POWER
    config.manage_panel_power = true;
#endif
    lv_display_t *display;
    err = candis_display_start(&config, &display);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Start AMOLED: %s", esp_err_to_name(err));
        recorder_deinit();
        return;
    }
    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "Lock AMOLED timed out");
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        recorder_deinit();
        return;
    }
    lv_timer_t *refresh = lv_timer_create(update_ui, 50, NULL);
    if (refresh) {
        create_ui(display);
    }
    bsp_display_unlock();
    if (!refresh) {
        ESP_LOGE(TAG, "Allocate the recorder UI timer");
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        recorder_deinit();
        return;
    }
    ESP_LOGI(TAG, "Recorder ready: %u s, %u Hz, %u channels",
             RECORDER_SECONDS, CANDIS_AUDIO_RATE, CANDIS_AUDIO_CHANNELS);
    recorder_run();
}
