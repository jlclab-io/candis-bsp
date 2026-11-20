/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "bsp/candis.h"
#include "bsp/candis_audio.h"
#include "candis_example_usb.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "music.h"
#include "sdkconfig.h"

enum {
    BLOCK_FRAMES = CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES,
    MAX_VOLUME = 100,
    DEFAULT_VOLUME = 50,
    IO_TIMEOUT_MS = 100,
    DRAIN_MS = 55, /* Ten 5 ms TX DMA buffers plus scheduling margin. */
};

typedef enum {
    ROUTE_LEFT = 1,
    ROUTE_RIGHT = 2,
    ROUTE_BOTH = ROUTE_LEFT | ROUTE_RIGHT,
} route_t;

typedef enum {
    PLAYER_READY,
    PLAYER_OPENING,
    PLAYER_PLAYING,
    PLAYER_STOPPING,
    PLAYER_ERROR,
} player_state_t;

static const char *TAG = "candis_music";
static const char *const s_state_labels[] = {
    "Ready", "Opening audio", "Playing", "Stopping", "Audio error - retry",
};
static atomic_bool s_play_requested;
static atomic_uint s_volume = DEFAULT_VOLUME;
static atomic_uint s_route = ROUTE_BOTH;
static atomic_uint s_state = PLAYER_READY;
static atomic_uint s_peak; /* Two 16-bit peaks published together. */

static struct {
    lv_obj_t *play;
    lv_obj_t *status;
    lv_obj_t *volume;
    lv_obj_t *volume_value;
    lv_obj_t *meter[CANDIS_AUDIO_CHANNELS];
    lv_obj_t *route[4];
} s_ui;

static const char *route_name(route_t route)
{
    if (route == ROUTE_LEFT) {
        return "left";
    }
    return route == ROUTE_RIGHT ? "right" : "both";
}

static void read_console(void)
{
    static char line[16];
    static size_t used;
    static bool overflow;
    char c;
    for (unsigned i = 0; i < 32 && candis_example_usb_read(&c); ++i) {
        if (c != '\n' && c != '\r') {
            if (used < sizeof(line) - 1) {
                line[used++] = c;
            } else {
                overflow = true;
            }
            continue;
        }
        line[used] = '\0';
        if (!overflow && used) {
            unsigned volume;
            char extra;
            if (strcmp(line, "PLAY") == 0) {
                atomic_store(&s_play_requested, true);
            } else if (strcmp(line, "STOP") == 0) {
                atomic_store(&s_play_requested, false);
            } else if (strcmp(line, "LEFT") == 0) {
                atomic_store(&s_route, ROUTE_LEFT);
            } else if (strcmp(line, "RIGHT") == 0) {
                atomic_store(&s_route, ROUTE_RIGHT);
            } else if (strcmp(line, "BOTH") == 0) {
                atomic_store(&s_route, ROUTE_BOTH);
            } else if (sscanf(line, "VOL %3u %c", &volume, &extra) == 1 && volume <= MAX_VOLUME) {
                atomic_store(&s_volume, volume);
            } else if (strcmp(line, "STATUS") == 0) {
                const unsigned peak = atomic_load(&s_peak);
                ESP_LOGI(TAG, "MUSIC_STATUS state=%s volume=%u route=%s pcm_peak_L=%u pcm_peak_R=%u",
                         s_state_labels[atomic_load(&s_state)], atomic_load(&s_volume),
                         route_name(atomic_load(&s_route)), peak & 0xffffU, peak >> 16);
            } else {
                ESP_LOGW(TAG, "Commands: PLAY STOP VOL <0-100> LEFT BOTH RIGHT STATUS");
            }
        }
        used = 0;
        overflow = false;
    }
}

static esp_err_t write_block(const int16_t *pcm, bool playback)
{
    size_t sent;
    esp_err_t err = playback ? candis_audio_playback_write(pcm, BLOCK_FRAMES, &sent, IO_TIMEOUT_MS) :
                               candis_audio_write(pcm, BLOCK_FRAMES, &sent, IO_TIMEOUT_MS);
    ESP_RETURN_ON_ERROR(err, TAG, "write I2S block");
    ESP_RETURN_ON_FALSE(sent == BLOCK_FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short I2S write");
    return ESP_OK;
}

static esp_err_t apply_volume(unsigned volume, unsigned previous)
{
    ESP_RETURN_ON_ERROR(candis_audio_set_volume(volume), TAG, "set speaker volume");
    if (volume && !previous) {
        ESP_RETURN_ON_ERROR(candis_audio_set_output(true), TAG, "enable speaker amplifier");
    }
    ESP_LOGI(TAG, "MUSIC_VOLUME volume=%u amp=%s", volume, volume ? "on" : "off");
    return ESP_OK;
}

static unsigned route_block(const int16_t *mono, int16_t *pcm, route_t route, bool fade_in, bool fade_out)
{
    unsigned peak[CANDIS_AUDIO_CHANNELS] = {0};
    for (unsigned i = 0; i < BLOCK_FRAMES; ++i) {
        int sample = mono[i];
        if (fade_in) {
            sample = sample * (int)i / (BLOCK_FRAMES - 1);
        }
        if (fade_out) {
            sample = sample * (int)(BLOCK_FRAMES - 1 - i) / (BLOCK_FRAMES - 1);
        }
        for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
            const int output = route & (1U << ch) ? sample : 0;
            pcm[i * CANDIS_AUDIO_CHANNELS + ch] = (int16_t)output;
            const unsigned level = output < 0 ? -output : output;
            if (level > peak[ch]) {
                peak[ch] = level;
            }
        }
    }
    return peak[0] | (peak[1] << 16);
}

static esp_err_t play_music(void)
{
    int16_t pcm[BLOCK_FRAMES * CANDIS_AUDIO_CHANNELS] = {0};
    int16_t mono[BLOCK_FRAMES];
    music_cursor_t cursor = {0};
    unsigned volume = atomic_load(&s_volume);
    route_t route = atomic_load(&s_route);
    bool fade_in = true;
    ESP_RETURN_ON_ERROR(write_block(pcm, false), TAG, "prime TX with silence");
    if (!atomic_load(&s_play_requested) || candis_example_usb_download_pending()) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(apply_volume(volume, 0), TAG, "initialize volume");
    ESP_LOGI(TAG, "MUSIC_ROUTE route=%s", route_name(route));
    atomic_store(&s_state, PLAYER_PLAYING);

    unsigned loops = 0;
    for (;;) {
        read_console();
        const bool stopping = !atomic_load(&s_play_requested) || candis_example_usb_download_pending();
        const unsigned next_volume = atomic_load(&s_volume);
        const route_t next_route = atomic_load(&s_route);
        if (!stopping && next_volume != volume) {
            ESP_RETURN_ON_ERROR(apply_volume(next_volume, volume), TAG, "change volume");
            fade_in |= volume == 0;
            volume = next_volume;
        }

        const bool loop_finished = music_render(&cursor, mono);
        const bool route_changed = next_route != route;
        const unsigned peak = route_block(mono, pcm, route, fade_in, stopping || route_changed);
        ESP_RETURN_ON_ERROR(write_block(pcm, true), TAG, "play music");
        atomic_store(&s_peak, volume ? peak : 0);
        if (stopping) {
            break;
        }
        if (loop_finished) {
            ESP_LOGI(TAG, "MUSIC_LOOP completed=%u", ++loops);
        }
        if (route_changed) {
            route = next_route;
            ESP_LOGI(TAG, "MUSIC_ROUTE route=%s", route_name(route));
        }
        fade_in = route_changed;
    }
    atomic_store(&s_state, PLAYER_STOPPING);
    vTaskDelay(pdMS_TO_TICKS(DRAIN_MS) + 1);
    return candis_audio_set_output(false);
}

static void play_requested(void)
{
    if (!candis_example_usb_begin_interruptible_operation()) {
        atomic_store(&s_play_requested, false);
        return;
    }
    atomic_store(&s_state, PLAYER_OPENING);
    bool manage_power = false;
#ifdef CONFIG_CANDIS_MUSIC_PLAYER_MANAGE_AUDIO_POWER
    manage_power = true;
#endif
    esp_err_t err = candis_audio_open(manage_power);
    if (err == ESP_OK) {
        err = play_music();
    }
    const esp_err_t cleanup = candis_audio_close();
    if (err != ESP_OK || cleanup != ESP_OK || candis_example_usb_download_pending()) {
        atomic_store(&s_play_requested, false);
    }
    atomic_store(&s_peak, 0);
    atomic_store(&s_state, err == ESP_OK && cleanup == ESP_OK ? PLAYER_READY : PLAYER_ERROR);
    ESP_LOGI(TAG, "MUSIC_STOP result=%s cleanup=%s", esp_err_to_name(err), esp_err_to_name(cleanup));
    candis_example_usb_end_operation();
}

static void on_play(lv_event_t *event)
{
    (void)event;
    atomic_store(&s_play_requested, !atomic_load(&s_play_requested));
}

static void on_volume(lv_event_t *event)
{
    atomic_store(&s_volume, lv_slider_get_value(lv_event_get_target_obj(event)));
}

static void on_route(lv_event_t *event)
{
    atomic_store(&s_route, (uintptr_t)lv_event_get_user_data(event));
}

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) {
        lv_label_set_text(label, text);
    }
}

static void update_ui(lv_timer_t *timer)
{
    (void)timer;
    const unsigned volume = atomic_load(&s_volume);
    const route_t route = atomic_load(&s_route);
    const unsigned peak = atomic_load(&s_peak);
    const bool play = atomic_load(&s_play_requested);
    set_text(s_ui.status, s_state_labels[atomic_load(&s_state)]);
    set_text(s_ui.play, play ? LV_SYMBOL_STOP "  Stop" : LV_SYMBOL_PLAY "  Play");

    char value[8];
    snprintf(value, sizeof(value), "%u%%", volume);
    set_text(s_ui.volume_value, value);
    if (!lv_slider_is_dragged(s_ui.volume) && lv_slider_get_value(s_ui.volume) != (int)volume) {
        lv_slider_set_value(s_ui.volume, volume, LV_ANIM_OFF);
    }
    for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
        const unsigned level = (peak >> (16 * ch)) & 0xffffU;
        lv_bar_set_value(s_ui.meter[ch], level, LV_ANIM_OFF);
    }
    for (route_t option = ROUTE_LEFT; option <= ROUTE_BOTH; ++option) {
        if (option == route) {
            lv_obj_add_state(s_ui.route[option], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_ui.route[option], LV_STATE_CHECKED);
        }
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static esp_err_t create_ui(lv_display_t *display)
{
    const uint32_t foreground = 0xeaf4ec;
    const uint32_t muted = 0x9fb3aa;
    const uint32_t track = 0x273933;
    const uint32_t accent = 0xa8eeb9;
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101b19), 0);

    lv_obj_set_pos(label(screen, "Music player", &lv_font_montserrat_20, muted), 30, 24);
    lv_obj_set_pos(label(screen, "First Light", &lv_font_montserrat_32, foreground), 30, 54);
    s_ui.status = label(screen, "Ready", &lv_font_montserrat_20, muted);
    lv_obj_set_pos(s_ui.status, 30, 101);

    for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
        const int y = 145 + 32 * ch;
        lv_obj_set_pos(label(screen, ch ? "R" : "L", &lv_font_montserrat_20, muted), 31, y - 4);
        lv_obj_t *bar = lv_bar_create(screen);
        lv_obj_set_pos(bar, 61, y);
        lv_obj_set_size(bar, 389, 14);
        lv_obj_set_style_bg_color(bar, lv_color_hex(track), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(accent), LV_PART_INDICATOR);
        lv_bar_set_range(bar, 0, 11000);
        s_ui.meter[ch] = bar;
    }

    lv_obj_set_pos(label(screen, "Volume", &lv_font_montserrat_20, foreground), 30, 222);
    s_ui.volume_value = label(screen, "50%", &lv_font_montserrat_20, accent);
    lv_obj_set_width(s_ui.volume_value, 76);
    lv_obj_set_style_text_align(s_ui.volume_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_ui.volume_value, 374, 222);
    s_ui.volume = lv_slider_create(screen);
    lv_obj_set_pos(s_ui.volume, 42, 264);
    lv_obj_set_size(s_ui.volume, 396, 14);
    lv_obj_set_ext_click_area(s_ui.volume, 13);
    lv_obj_set_style_bg_color(s_ui.volume, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.volume, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.volume, lv_color_hex(foreground), LV_PART_KNOB);
    lv_slider_set_range(s_ui.volume, 0, MAX_VOLUME);
    lv_slider_set_value(s_ui.volume, DEFAULT_VOLUME, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.volume, on_volume, LV_EVENT_VALUE_CHANGED, NULL);

    const route_t routes[] = {ROUTE_LEFT, ROUTE_BOTH, ROUTE_RIGHT};
    const char *const captions[] = {"Left", "Stereo", "Right"};
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        lv_obj_t *obj = button(screen, 30 + 144 * i, 313, 132, 54, track);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x366c54), LV_STATE_CHECKED);
        lv_obj_add_event_cb(obj, on_route, LV_EVENT_CLICKED, (void *)(uintptr_t)routes[i]);
        lv_obj_center(label(obj, captions[i], &lv_font_montserrat_20, foreground));
        s_ui.route[routes[i]] = obj;
    }

    lv_obj_t *play = button(screen, 30, 386, 420, 64, accent);
    s_ui.play = label(play, "", &lv_font_montserrat_20, 0x10271c);
    lv_obj_center(s_ui.play);
    lv_obj_add_event_cb(play, on_play, LV_EVENT_CLICKED, NULL);
    update_ui(NULL);
    return lv_timer_create(update_ui, 50, NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    music_init();
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_MUSIC_PLAYER_MANAGE_PANEL_POWER
    config.manage_panel_power = true;
#endif
    lv_display_t *display;
    if (candis_display_start(&config, &display) == ESP_OK) {
        esp_err_t err = ESP_ERR_TIMEOUT;
        if (bsp_display_lock(1000)) {
            err = create_ui(display);
            bsp_display_unlock();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Create touchscreen player: %s", esp_err_to_name(err));
            ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        }
    } else {
        ESP_LOGE(TAG, "AMOLED unavailable, use CDC commands");
    }

    ESP_LOGI(TAG, "MUSIC_READY First Light, 48000 Hz stereo, volume=%u", DEFAULT_VOLUME);
    for (;;) {
        read_console();
        if (atomic_load(&s_play_requested)) {
            play_requested();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
