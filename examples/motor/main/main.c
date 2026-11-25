/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "bsp/candis.h"
#include "bsp/candis_motor.h"
#include "bsp/candis_power.h"
#include "candis_example_usb.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tg28_esp.h"

static const char *TAG = "candis_motor";

enum {
    MOTOR_AMPLITUDE_DEFAULT = 50,
    MOTOR_AMPLITUDE_MIN = 1,
    MOTOR_AMPLITUDE_MAX = 100,
    MOTOR_FREQUENCY_HZ = 20000,
    MOTOR_FREQUENCY_KHZ_MIN = 1,
    MOTOR_FREQUENCY_KHZ_MAX = 20,
    MOTOR_RAIL_MV = 2700,
    MOTOR_RAIL_SETTLE_MS = 20,
    UI_PERIOD_MS = 100,
    POWER_PERIOD_MS = 1000,
};

typedef struct {
    bool present;
    bool charging;
    bool vbus_good;
    bool soc_valid;
    uint8_t soc;
    uint8_t adc_mask;
    uint16_t charge_current_ma;
    uint32_t battery_uv;
    esp_err_t vbat_error;
    esp_err_t soc_error;
    esp_err_t error;
} battery_sample_t;

typedef struct {
    lv_obj_t *soc_label;
    lv_obj_t *battery_label;
    lv_obj_t *charge_label;
    lv_obj_t *output_label;
    lv_obj_t *rail_label;
    lv_obj_t *amplitude_label;
    lv_obj_t *frequency_label;
    lv_obj_t *run_button;
    lv_obj_t *run_label;
    lv_obj_t *amplitude_slider;
    lv_obj_t *frequency_slider;
    lv_obj_t *error_label;
} motor_ui_t;

static candis_motor_config_t s_motor_config;
static atomic_bool s_requested_run;
static atomic_uint s_requested_amplitude = ATOMIC_VAR_INIT(MOTOR_AMPLITUDE_DEFAULT);
static atomic_uint s_requested_frequency = ATOMIC_VAR_INIT(MOTOR_FREQUENCY_HZ);
static motor_ui_t s_ui;
static i2c_master_bus_handle_t s_pmu_bus;
static tg28_esp_t s_pmu;
static bool s_pmu_ready;
static bool s_charge_default_applied;
static bool s_download_guard;
static bool s_manual_run;
static unsigned s_applied_amplitude;
static unsigned s_applied_frequency;
static esp_err_t s_last_error;
static int64_t s_next_charge_retry_us;

static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x102028), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x29414b), 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int width, int height, uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x071014), 0);
    lv_label_set_text(label, text);
    return button;
}

static void update_control_labels(unsigned amplitude, unsigned frequency_hz)
{
    char text[48];
    if (s_ui.amplitude_label) {
        const unsigned duty = candis_motor_amplitude_to_duty(amplitude);
        snprintf(text, sizeof(text), "AMPLITUDE %u%%  /  PWM %u%%", amplitude, duty);
        set_label(s_ui.amplitude_label, text);
    }
    if (s_ui.frequency_label) {
        snprintf(text, sizeof(text), "PWM FREQ %u kHz", frequency_hz / 1000U);
        set_label(s_ui.frequency_label, text);
    }
}

static void set_requested_amplitude(unsigned amplitude)
{
    if (amplitude < MOTOR_AMPLITUDE_MIN) {
        amplitude = MOTOR_AMPLITUDE_MIN;
    } else if (amplitude > MOTOR_AMPLITUDE_MAX) {
        amplitude = MOTOR_AMPLITUDE_MAX;
    }
    atomic_store(&s_requested_amplitude, amplitude);
    update_control_labels(amplitude, atomic_load(&s_requested_frequency));
}

static void set_requested_frequency(unsigned frequency_hz)
{
    frequency_hz = frequency_hz < MOTOR_FREQUENCY_KHZ_MIN * 1000U
                       ? MOTOR_FREQUENCY_KHZ_MIN * 1000U
                       : frequency_hz;
    frequency_hz = frequency_hz > MOTOR_FREQUENCY_KHZ_MAX * 1000U
                       ? MOTOR_FREQUENCY_KHZ_MAX * 1000U
                       : frequency_hz;
    atomic_store(&s_requested_frequency, frequency_hz);
    update_control_labels(atomic_load(&s_requested_amplitude), frequency_hz);
}

static void slider_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);
    if (slider == s_ui.amplitude_slider) {
        set_requested_amplitude((unsigned)lv_slider_get_value(slider));
    } else {
        set_requested_frequency((unsigned)lv_slider_get_value(slider) * 1000U);
    }
}

static void toggle_run(lv_event_t *event)
{
    (void)event;
    atomic_store(&s_requested_run, !atomic_load(&s_requested_run));
}

static void create_ui(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071014), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    (void)make_label(screen, "MOTOR LAB", 22, 14, &lv_font_montserrat_28, 0xffb547);
    make_label(screen, "GPIO5  /  BLDO2 2.7 V  /  1-20 kHz", 24, 49,
               &lv_font_montserrat_14, 0x8ca3aa);

    lv_obj_t *battery = make_card(screen, 20, 80, 440, 110);
    make_label(battery, "BATTERY", 18, 12, &lv_font_montserrat_14, 0x8ca3aa);
    s_ui.soc_label = make_label(battery, "--%", 18, 36, &lv_font_montserrat_36,
                                0xe8f2ec);
    s_ui.battery_label = make_label(battery, "Battery not detected\nVBAT ADC invalid", 145, 34,
                                   &lv_font_montserrat_20, 0xe8f2ec);
    lv_obj_set_width(s_ui.battery_label, 275);
    s_ui.charge_label = make_label(battery, "USB POWER", 270, 12,
                                  &lv_font_montserrat_14, 0x8ca3aa);
    lv_obj_set_width(s_ui.charge_label, 150);
    lv_obj_set_style_text_align(s_ui.charge_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *output = make_card(screen, 20, 202, 440, 72);
    make_label(output, "OUTPUT", 18, 12, &lv_font_montserrat_14, 0x8ca3aa);
    s_ui.output_label = make_label(output, "IDLE", 112, 9, &lv_font_montserrat_20,
                                   0xe8f2ec);
    s_ui.rail_label = make_label(output, "BLDO2 --", 18, 40, &lv_font_montserrat_14,
                                 0xb8c7c2);

    s_ui.run_button = make_button(screen, "START VIBRATION", 20, 286, 440, 64,
                                  0xffb547);
    s_ui.run_label = lv_obj_get_child(s_ui.run_button, 0);
    lv_obj_add_event_cb(s_ui.run_button, toggle_run, LV_EVENT_CLICKED, NULL);

    lv_obj_t *control = make_card(screen, 20, 358, 440, 96);
    s_ui.amplitude_label = make_label(control, "AMPLITUDE 50%  /  PWM 60%", 18, 7,
                                      &lv_font_montserrat_14, 0xffb547);
    s_ui.frequency_label = make_label(control, "PWM FREQ 20 kHz", 280, 7,
                                      &lv_font_montserrat_14, 0x8ca3aa);
    s_ui.amplitude_slider = lv_slider_create(control);
    lv_obj_set_pos(s_ui.amplitude_slider, 18, 30);
    lv_obj_set_size(s_ui.amplitude_slider, 220, 22);
    lv_slider_set_range(s_ui.amplitude_slider, MOTOR_AMPLITUDE_MIN, MOTOR_AMPLITUDE_MAX);
    lv_slider_set_value(s_ui.amplitude_slider, MOTOR_AMPLITUDE_DEFAULT, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.amplitude_slider, slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(s_ui.amplitude_slider, lv_color_hex(0x29414b), 0);
    lv_obj_set_style_bg_color(s_ui.amplitude_slider, lv_color_hex(0xffb547), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.amplitude_slider, lv_color_hex(0xffb547), LV_PART_KNOB);

    s_ui.frequency_slider = lv_slider_create(control);
    lv_obj_set_pos(s_ui.frequency_slider, 260, 30);
    lv_obj_set_size(s_ui.frequency_slider, 162, 22);
    lv_slider_set_range(s_ui.frequency_slider, MOTOR_FREQUENCY_KHZ_MIN,
                        MOTOR_FREQUENCY_KHZ_MAX);
    lv_slider_set_value(s_ui.frequency_slider, MOTOR_FREQUENCY_HZ / 1000U, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.frequency_slider, slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(s_ui.frequency_slider, lv_color_hex(0x29414b), 0);
    lv_obj_set_style_bg_color(s_ui.frequency_slider, lv_color_hex(0x66d9a0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.frequency_slider, lv_color_hex(0x66d9a0), LV_PART_KNOB);

    s_ui.error_label = make_label(screen, "", 24, 458, &lv_font_montserrat_14,
                                  0xff8b82);
    update_control_labels(MOTOR_AMPLITUDE_DEFAULT, MOTOR_FREQUENCY_HZ);
}

static esp_err_t open_pmu(void)
{
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_pmu_bus), TAG, "acquire PMU I2C");
    const tg28_esp_config_t config = {
        .address = 0x34,
        .scl_speed_hz = 100000,
        .timeout_ms = 100,
    };
    const esp_err_t err = tg28_esp_open(&s_pmu, s_pmu_bus, &config);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_i2c_release(s_pmu_bus));
        s_pmu_bus = NULL;
        return err;
    }
    uint8_t chip_id;
    if (tg28_esp_read_chip_id(&s_pmu, &chip_id) != ESP_OK || chip_id != TG28_ESP_CHIP_ID) {
        (void)tg28_esp_close(&s_pmu);
        (void)candis_i2c_release(s_pmu_bus);
        s_pmu_bus = NULL;
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_pmu_ready = true;
    return ESP_OK;
}

static esp_err_t sample_battery(battery_sample_t *sample)
{
    *sample = (battery_sample_t){0};
    if (!s_pmu_ready) {
        sample->error = ESP_ERR_INVALID_STATE;
        return sample->error;
    }
    tg28_esp_status_t status;
    esp_err_t err = tg28_esp_read_status(&s_pmu, &status);
    if (err != ESP_OK) {
        sample->error = err;
        return err;
    }
    sample->present = status.battery_present;
    sample->charging = status.direction == TG28_ESP_BATTERY_CHARGING;
    sample->vbus_good = status.vbus_good;
    tg28_esp_charger_t charger;
    if (tg28_esp_read_charger(&s_pmu, &charger) == ESP_OK) {
        sample->charge_current_ma = charger.current_ma;
    }

    sample->vbat_error = tg28_esp_read_adc_mask(&s_pmu, &sample->adc_mask);
    if (sample->vbat_error == ESP_OK && (sample->adc_mask & TG28_ESP_ADC_VBAT)) {
        sample->vbat_error = tg28_esp_read_adc_uv(&s_pmu, TG28_ESP_ADC_VBAT, &sample->battery_uv);
    } else if (sample->vbat_error == ESP_OK) {
        sample->vbat_error = ESP_ERR_INVALID_STATE;
    }

    if (!sample->present && sample->vbat_error == ESP_OK &&
        sample->battery_uv >= 2500000 && sample->battery_uv <= 4500000) {
        sample->present = true;
    }
    if (sample->present) {
        sample->soc_error = tg28_esp_read_soc(&s_pmu, &sample->soc);
        if (sample->soc_error == ESP_OK) {
            sample->soc = sample->soc > 100 ? 100 : sample->soc;
            sample->soc_valid = true;
        }
    }
    return ESP_OK;
}

static void update_dashboard(const battery_sample_t *battery, const candis_motor_state_t *motor,
                             bool requested_run, unsigned requested_amplitude,
                             unsigned requested_frequency)
{
    if (!s_ui.soc_label || !bsp_display_lock(50)) {
        return;
    }
    char text[64];
    if (battery->error != ESP_OK) {
        set_label(s_ui.soc_label, "--%");
        set_label(s_ui.battery_label, "PMU unavailable");
        set_label(s_ui.charge_label, esp_err_to_name(battery->error));
    } else if (!battery->present) {
        set_label(s_ui.soc_label, "--%");
        set_label(s_ui.battery_label, "Battery not detected\nVBAT ADC invalid");
        set_label(s_ui.charge_label, battery->vbus_good ? "USB POWER" : "NO USB");
    } else {
        if (battery->soc_valid) {
            snprintf(text, sizeof(text), "%u%%", battery->soc);
        } else {
            snprintf(text, sizeof(text), "--%%");
        }
        set_label(s_ui.soc_label, text);
        const char *power_state = battery->charging ? "CHARGING" :
                                  battery->vbus_good ? "USB POWER" : "DISCHARGING";
        if (battery->vbat_error == ESP_OK) {
            snprintf(text, sizeof(text), "VBAT %" PRIu32 ".%03" PRIu32 " V\n%s",
                     battery->battery_uv / 1000000U, (battery->battery_uv % 1000000U) / 1000U,
                     power_state);
        } else {
            snprintf(text, sizeof(text), "VBAT --\n%s", power_state);
        }
        set_label(s_ui.battery_label, text);
        snprintf(text, sizeof(text), "ICC %u mA", battery->charge_current_ma);
        set_label(s_ui.charge_label, text);
    }

    snprintf(text, sizeof(text), "%s", motor->active ? "RUNNING" : "IDLE");
    set_label(s_ui.output_label, text);
    const unsigned duty = motor->active ? motor->duty_percent :
                          candis_motor_amplitude_to_duty(requested_amplitude);
    snprintf(text, sizeof(text), "BLDO2 %u mV  /  PWM %u%%  /  AMP %u%%",
             s_motor_config.supply_mv, duty, requested_amplitude);
    set_label(s_ui.rail_label, text);
    update_control_labels(requested_amplitude, requested_frequency);
    set_label(s_ui.run_label, requested_run ? "STOP VIBRATION" : "START VIBRATION");
    lv_obj_set_style_bg_color(s_ui.run_button,
                              lv_color_hex(requested_run ? 0xff6b6b : 0xffb547), 0);
    set_label(s_ui.error_label, s_last_error == ESP_OK ? "" : esp_err_to_name(s_last_error));
    lv_slider_set_value(s_ui.amplitude_slider, requested_amplitude, LV_ANIM_OFF);
    lv_slider_set_value(s_ui.frequency_slider, requested_frequency / 1000U, LV_ANIM_OFF);
    bsp_display_unlock();
}

static void release_download_guard(void)
{
    candis_motor_state_t state;
    if (s_download_guard && candis_motor_get_state(&state) == ESP_OK &&
        !state.active && !state.cleanup_pending) {
        candis_example_usb_end_operation();
        s_download_guard = false;
        if (state.last_error != ESP_OK) {
            s_last_error = state.last_error;
        }
    }
}

static esp_err_t start_continuous(unsigned amplitude, unsigned frequency_hz)
{
    if (s_download_guard || !candis_example_usb_begin_operation()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = candis_motor_set_frequency(frequency_hz);
    if (err == ESP_OK) {
        err = candis_motor_start_amplitude(amplitude);
    }
    if (err == ESP_OK) {
        s_download_guard = true;
        s_manual_run = true;
        s_applied_amplitude = amplitude;
        s_applied_frequency = frequency_hz;
    } else {
        candis_example_usb_end_operation();
    }
    return err;
}

static esp_err_t start_pulse(unsigned amplitude)
{
    if (s_download_guard || !candis_example_usb_begin_operation()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = candis_motor_pulse_amplitude(amplitude, CANDIS_MOTOR_MAX_PULSE_MS);
    if (err == ESP_OK) {
        s_download_guard = true;
        s_manual_run = false;
        s_applied_amplitude = amplitude;
    } else {
        candis_example_usb_end_operation();
    }
    return err;
}

static void stop_continuous(void)
{
    atomic_store(&s_requested_run, false);
    s_manual_run = false;
    s_last_error = candis_motor_stop();
    release_download_guard();
}

static void report_status(void)
{
    battery_sample_t battery;
    candis_motor_state_t motor;
    const esp_err_t battery_err = sample_battery(&battery);
    const esp_err_t motor_err = candis_motor_get_state(&motor);
    ESP_LOGI(TAG,
             "STATUS battery=%s present=%d vbus=%d vbat=%" PRIu32 "mV vbat_err=%s soc=%d soc_err=%s "
             "adc=0x%02x charge=%u mA charging=%d motor=%s active=%d last=%s",
             esp_err_to_name(battery_err), battery.present, battery.vbus_good,
             battery.battery_uv / 1000U, esp_err_to_name(battery.vbat_error),
             battery.soc_valid ? battery.soc : -1, esp_err_to_name(battery.soc_error),
             battery.adc_mask, battery.charge_current_ma, battery.charging,
             esp_err_to_name(motor_err), motor.active, esp_err_to_name(s_last_error));
}

static void command(const char *line)
{
    unsigned value = 0;
    if (strcmp(line, "STATUS") == 0) {
        report_status();
    } else if (strcmp(line, "STOP") == 0) {
        stop_continuous();
    } else if (strcmp(line, "HELP") == 0) {
        puts("Commands: STATUS, RUN 1-100, AMP 1-100, FREQ 1000-20000, PULSE 1-100, STOP");
    } else if (sscanf(line, "RUN %u", &value) == 1 &&
               value >= MOTOR_AMPLITUDE_MIN && value <= MOTOR_AMPLITUDE_MAX) {
        set_requested_amplitude(value);
        atomic_store(&s_requested_run, true);
    } else if (sscanf(line, "AMP %u", &value) == 1 &&
               value >= MOTOR_AMPLITUDE_MIN && value <= MOTOR_AMPLITUDE_MAX) {
        set_requested_amplitude(value);
    } else if (sscanf(line, "FREQ %u", &value) == 1 &&
               value >= MOTOR_FREQUENCY_KHZ_MIN * 1000U &&
               value <= MOTOR_FREQUENCY_KHZ_MAX * 1000U) {
        set_requested_frequency(value);
    } else if (sscanf(line, "PULSE %u", &value) == 1 &&
               value >= MOTOR_AMPLITUDE_MIN && value <= MOTOR_AMPLITUDE_MAX) {
        const esp_err_t err = start_pulse(value);
        if (err != ESP_OK) {
            s_last_error = err;
        }
    } else {
        puts("Unknown command. Send HELP.");
    }
}

static void control_loop(void)
{
    int64_t next_battery = 0;
    int64_t next_ui = 0;
    char line[24];
    size_t length = 0;
    bool overflow = false;
    battery_sample_t battery = {.error = ESP_ERR_INVALID_STATE};

    for (;;) {
        const int64_t now = esp_timer_get_time();
        const bool requested_run = atomic_load(&s_requested_run);
        const unsigned requested_amplitude = atomic_load(&s_requested_amplitude);
        const unsigned requested_frequency = atomic_load(&s_requested_frequency);
        candis_motor_state_t state = {0};
        esp_err_t err = candis_motor_get_state(&state);
        if (err == ESP_OK && requested_run && !state.active && !state.cleanup_pending) {
            err = start_continuous(requested_amplitude, requested_frequency);
            if (err != ESP_OK) {
                atomic_store(&s_requested_run, false);
                s_last_error = err;
            } else {
                s_last_error = ESP_OK;
            }
        } else if (err == ESP_OK && requested_run && state.active) {
            if (requested_frequency != s_applied_frequency) {
                err = candis_motor_set_frequency(requested_frequency);
                if (err == ESP_OK) {
                    s_applied_frequency = requested_frequency;
                } else {
                    s_last_error = err;
                }
            }
            if (err == ESP_OK && requested_amplitude != s_applied_amplitude) {
                err = candis_motor_set_amplitude(requested_amplitude);
                if (err == ESP_OK) {
                    s_applied_amplitude = requested_amplitude;
                    s_last_error = ESP_OK;
                } else {
                    s_last_error = err;
                }
            }
        } else if (err == ESP_OK && !requested_run && s_manual_run && state.active) {
            stop_continuous();
        }
        release_download_guard();

        if (now >= next_battery) {
            next_battery = now + POWER_PERIOD_MS * 1000LL;
            (void)sample_battery(&battery);
            if (!s_charge_default_applied && battery.present && battery.vbus_good &&
                now >= s_next_charge_retry_us) {
                const esp_err_t charge_err = candis_power_ensure_default_charge();
                if (charge_err == ESP_OK) {
                    s_charge_default_applied = true;
                    ESP_LOGI(TAG, "DEFAULT_CHARGE_APPLIED 200 mA");
                } else {
                    s_next_charge_retry_us = now + 5 * 1000 * 1000LL;
                    ESP_LOGW(TAG, "Default charge retry deferred: %s", esp_err_to_name(charge_err));
                }
            }
        }
        if (now >= next_ui) {
            next_ui = now + UI_PERIOD_MS * 1000LL;
            if (candis_motor_get_state(&state) == ESP_OK) {
                update_dashboard(&battery, &state, requested_run, requested_amplitude,
                                 requested_frequency);
            }
        }

        char next;
        while (candis_example_usb_read(&next)) {
            if (next == '\r' || next == '\n') {
                if (length && !overflow) {
                    line[length] = '\0';
                    command(line);
                }
                length = 0;
                overflow = false;
            } else if (length < sizeof(line) - 1) {
                line[length++] = next;
            } else {
                overflow = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    s_motor_config = (candis_motor_config_t)CANDIS_MOTOR_CONFIG_DEFAULT();
    s_motor_config.pwm_gpio = GPIO_NUM_5;
    s_motor_config.frequency_hz = MOTOR_FREQUENCY_HZ;
    s_motor_config.use_ldo = true;
    s_motor_config.supply_rail = CANDIS_EXPANSION_BLDO2;
    s_motor_config.supply_mv = MOTOR_RAIL_MV;
    s_motor_config.supply_settle_ms = MOTOR_RAIL_SETTLE_MS;
    s_last_error = candis_motor_init(&s_motor_config);
    if (s_last_error != ESP_OK) {
        ESP_LOGE(TAG, "Motor configuration: %s", esp_err_to_name(s_last_error));
    }

    const esp_err_t pmu_err = open_pmu();
    if (pmu_err != ESP_OK) {
        s_last_error = pmu_err;
        ESP_LOGW(TAG, "Battery monitor unavailable: %s", esp_err_to_name(pmu_err));
    }

    candis_display_config_t display_config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    display_config.manage_panel_power = true;
    lv_display_t *display = NULL;
    const esp_err_t display_err = candis_display_start(&display_config, &display);
    if (display_err == ESP_OK && bsp_display_lock(1000)) {
        create_ui(display);
        bsp_display_unlock();
        ESP_LOGI(TAG, "MOTOR_UI_READY GPIO5 PWM=%dHz BLDO2=%dmV", MOTOR_FREQUENCY_HZ, MOTOR_RAIL_MV);
    } else {
        ESP_LOGW(TAG, "AMOLED UI unavailable: %s", esp_err_to_name(display_err));
    }
    puts("MOTOR_READY commands=STATUS,RUN 1-100,AMP 1-100,FREQ 1000-20000,PULSE 1-100,STOP");
    control_loop();
}
