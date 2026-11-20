/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <stdio.h>
#include <string.h>
#include "bsp/candis.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tg28_esp.h"

static const char *TAG = "candis_power_demo";

#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_25MA
#define CHARGE_TEST_LABEL "25 mA"
#else
#define CHARGE_TEST_LABEL "200 mA"
#endif

enum {
    CHARGE_BASELINE_MA = 50,
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_25MA
    CHARGE_TEST_MA = 25,
#else
    CHARGE_TEST_MA = 200,
#endif
    CHARGE_TEST_SECONDS = 15,
    CONFIRM_SECONDS = 8,
    START_TEST = 1,
    STOP_TEST = 2,
};

typedef struct {
    tg28_esp_status_t status;
    tg28_esp_charger_t charger;
    tg28_esp_irq_t irq;
    uint32_t battery_uv;
    uint8_t dcdc;
    uint8_t ldo;
    uint8_t soc;
    uint8_t adc;
    bool soc_valid;
} sample_t;

static tg28_esp_t s_pmu;
static lv_obj_t *s_current;
static lv_obj_t *s_metrics;
static lv_obj_t *s_details;
static lv_obj_t *s_note;
static const char *s_message = "Monitor";
static int64_t s_press_us;

#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
static TaskHandle_t s_monitor;
static int64_t s_deadline_us;
static int64_t s_confirm_until_us;
static bool s_restore_pending;
static bool s_charge_disabled;
static bool s_test_locked;
static lv_obj_t *s_start_label;
#endif

static esp_err_t sample_power(sample_t *sample)
{
    *sample = (sample_t){0};
    esp_err_t err;
    if ((err = tg28_esp_read_status(&s_pmu, &sample->status)) != ESP_OK ||
        (err = tg28_esp_read_charger(&s_pmu, &sample->charger)) != ESP_OK ||
        (err = tg28_esp_read_irq(&s_pmu, &sample->irq)) != ESP_OK ||
        (err = tg28_esp_read_dcdc_outputs(&s_pmu, &sample->dcdc)) != ESP_OK ||
        (err = tg28_esp_read_outputs(&s_pmu, &sample->ldo)) != ESP_OK ||
        (err = tg28_esp_read_adc_mask(&s_pmu, &sample->adc)) != ESP_OK) {
        return err;
    }
    if (sample->status.battery_present) {
        sample->soc_valid = tg28_esp_read_soc(&s_pmu, &sample->soc) == ESP_OK;
        if (sample->adc & TG28_ESP_ADC_VBAT) {
            return tg28_esp_read_adc_uv(&s_pmu, TG28_ESP_ADC_VBAT, &sample->battery_uv);
        }
    }
    return ESP_OK;
}

static const char *phase_name(tg28_esp_charge_state_t phase)
{
    static const char *const names[] = {"Trickle", "Precharge", "Constant current", "Constant voltage",
                                        "Charge done", "Not charging", "Unknown"};
    return names[phase];
}

static void set_label(lv_obj_t *label, const char *value)
{
    if (strcmp(lv_label_get_text(label), value) != 0) {
        lv_label_set_text(label, value);
    }
}

#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
static const char *charge_block_reason(esp_err_t err, const sample_t *sample)
{
    if (err != ESP_OK) {
        return esp_err_to_name(err);
    }
    const tg28_esp_status_t *status = &sample->status;
    const tg28_esp_charger_t *charger = &sample->charger;
    if (!status->battery_present || !status->vbus_good) {
        return status->battery_present ? "Charger input absent" : "Battery absent";
    }
    if (status->thermal_regulation || status->input_current_limit || status->input_voltage_limit) {
        return "PMU thermal or input limiting";
    }
    if ((sample->irq.bank[0] & 0x0f) || (sample->irq.bank[2] & 0x07)) {
        return "Battery or charger fault IRQ";
    }
    if (!charger->enabled || !charger->detection_enabled ||
        !charger->precharge_timer_enabled || !charger->charge_timer_enabled || charger->voltage_mv != 4200) {
        return "Charger settings outside test limits";
    }
    if (sample->battery_uv < 3200000 || sample->battery_uv > 4100000) {
        return "VBAT must be 3.20-4.10 V";
    }
    if (status->direction != TG28_ESP_BATTERY_CHARGING ||
        status->charge_state != TG28_ESP_CONSTANT_CURRENT) {
        return "Battery is not in constant-current charge";
    }
    return NULL;
}

static esp_err_t set_verified_current(uint16_t target_ma)
{
    esp_err_t err = tg28_esp_set_charge_current(&s_pmu, target_ma);
    uint16_t actual_ma = 0;
    if (err == ESP_OK) {
        err = tg28_esp_read_charge_current(&s_pmu, &actual_ma);
    }
    if (err == ESP_OK && actual_ma != target_ma) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "ICC setpoint target=%u mA readback=%u mA result=%s",
             target_ma, actual_ma, esp_err_to_name(err));
    return err;
}

static void restore_current(void)
{
    s_deadline_us = 0;
    if (set_verified_current(CHARGE_BASELINE_MA) == ESP_OK) {
        s_restore_pending = false;
        candis_example_usb_end_operation();
        s_message = s_charge_disabled ? "50 mA restored. Charger disabled." : "50 mA restored.";
        return;
    }
    s_test_locked = true;
    esp_err_t err = tg28_esp_set_charger_enabled(&s_pmu, false);
    tg28_esp_charger_t charger;
    s_charge_disabled |= err == ESP_OK && tg28_esp_read_charger(&s_pmu, &charger) == ESP_OK && !charger.enabled;
    s_message = s_charge_disabled ? "Charger off. Retrying restore." : "Restore failed. Cut charger input.";
    ESP_LOGE(TAG, "%s", s_message);
}

static bool handle_charge(uint32_t command, esp_err_t err, const sample_t *sample, int64_t now)
{
    const char *blocked = charge_block_reason(err, sample);
    if (s_restore_pending) {
        if (blocked || (command & STOP_TEST) || sample->charger.current_ma != CHARGE_TEST_MA || now >= s_deadline_us) {
            if (blocked) {
                ESP_LOGW(TAG, "Charge test abort: %s", blocked);
            }
            restore_current();
            return true;
        }
        return false;
    }
    if (s_test_locked) {
        return false;
    }
    if ((command & STOP_TEST) || (s_confirm_until_us && now >= s_confirm_until_us)) {
        s_confirm_until_us = 0;
        s_message = "Test canceled";
        return false;
    }
    if (!(command & START_TEST)) {
        return false;
    }
    if (blocked || sample->charger.current_ma != CHARGE_BASELINE_MA) {
        s_confirm_until_us = 0;
        s_message = blocked ? blocked : "Requires the original 50 mA setting";
        ESP_LOGW(TAG, "Charge test blocked: %s direction=%u timers=%u/%u",
                 s_message, sample->status.direction, sample->charger.precharge_timer_enabled,
                 sample->charger.charge_timer_enabled);
        return false;
    }
    if (!s_confirm_until_us) {
        s_confirm_until_us = now + CONFIRM_SECONDS * 1000000LL;
        s_message = "Confirm " CHARGE_TEST_LABEL " within 8 seconds";
        ESP_LOGI(TAG, "Charge test awaiting confirmation");
        return false;
    }
    s_confirm_until_us = 0;
    if (!candis_example_usb_begin_operation()) {
        s_message = "USB download in progress";
        return false;
    }
    /* The write can reach the PMU even if I2C reports an error. */
    s_restore_pending = true;
    s_deadline_us = now + CHARGE_TEST_SECONDS * 1000000LL;
    if (set_verified_current(CHARGE_TEST_MA) != ESP_OK) {
        s_test_locked = true;
        restore_current();
    } else {
        s_message = "Supervised test";
    }
    return true;
}

static void charge_button(lv_event_t *event)
{
    const uint32_t command = (uintptr_t)lv_event_get_user_data(event);
    xTaskNotify(s_monitor, command, eSetBits);
}

static lv_obj_t *action_button(lv_obj_t *screen, const char *name, int x, uintptr_t command, uint32_t color)
{
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_pos(button, x, 396);
    lv_obj_set_size(button, 200, 54);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_add_event_cb(button, charge_button, LV_EVENT_CLICKED, (void *)command);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, name);
    lv_obj_center(label);
    return label;
}
#endif

static lv_obj_t *text_at(lv_obj_t *screen, int y, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_obj_set_pos(label, 28, y);
    lv_obj_set_width(label, 424);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}

static void test_touch(lv_event_t *event)
{
    lv_obj_t *label = lv_event_get_user_data(event);
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        s_press_us = esp_timer_get_time();
        set_label(label, "Pressed");
        break;
    case LV_EVENT_LONG_PRESSED:
        set_label(label, "Holding");
        ESP_LOGI(TAG, "Touch test long press after %ld ms", (long)((esp_timer_get_time() - s_press_us) / 1000));
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        if (s_press_us) {
            ESP_LOGI(TAG, "Touch test %s after %ld ms",
                     lv_event_get_code(event) == LV_EVENT_RELEASED ? "release" : "lost",
                     (long)((esp_timer_get_time() - s_press_us) / 1000));
            s_press_us = 0;
        }
        set_label(label, "Touch test");
        break;
    default:
        break;
    }
}

static void create_ui(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101613), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    text_at(screen, 26, &lv_font_montserrat_20, 0xeff5ed, "Candis / TG28-ESP");
    lv_obj_t *touch_test = lv_button_create(screen);
    lv_obj_set_pos(touch_test, 300, 18);
    lv_obj_set_size(touch_test, 152, 42);
    lv_obj_set_style_radius(touch_test, 8, 0);
    lv_obj_set_style_bg_color(touch_test, lv_color_hex(0x39423d), 0);
    lv_obj_t *touch_label = lv_label_create(touch_test);
    lv_label_set_text(touch_label, "Touch test");
    lv_obj_center(touch_label);
    lv_obj_add_event_cb(touch_test, test_touch, LV_EVENT_ALL, touch_label);
    text_at(screen, 77, &lv_font_montserrat_20, 0xaebbb1, "Charge current setpoint");
    s_current = text_at(screen, 108, &lv_font_montserrat_32, 0x88e4b2, "-- mA");
    s_metrics = text_at(screen, 171, &lv_font_montserrat_20, 0xeff5ed, "Reading PMU");
    lv_obj_set_style_text_line_space(s_metrics, 7, 0);
    s_details = text_at(screen, 269, &lv_font_montserrat_20, 0xaebbb1, "");
    lv_obj_set_style_text_line_space(s_details, 6, 0);
    s_note = text_at(screen, 353, &lv_font_montserrat_20, 0xffcc80, "");
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
    s_start_label = action_button(screen, CHARGE_TEST_LABEL " test", 28, START_TEST, 0x266449);
    action_button(screen, "Stop test", 252, STOP_TEST, 0x843f30);
#endif
}

static void update_ui(esp_err_t err, const sample_t *sample)
{
    if (!s_current || !bsp_display_lock(20)) {
        return;
    }
    char line[115];
    if (err == ESP_OK) {
        char battery[44] = "Battery absent";
        char soc[15] = "SOC --";
        if (sample->status.battery_present) {
            strcpy(battery, "Battery present / VBAT --");
        }
        if (sample->battery_uv) {
            snprintf(battery, sizeof(battery), "VBAT %lu mV", (unsigned long)(sample->battery_uv / 1000));
        }
        if (sample->soc_valid) {
            snprintf(soc, sizeof(soc), "SOC %u%%", sample->soc);
        }
        snprintf(line, sizeof(line), "%u mA", sample->charger.current_ma);
        set_label(s_current, line);
        snprintf(line, sizeof(line), "%s   %s\nVBUS %s    %s", battery, soc,
                 sample->status.vbus_good ? "OK" : "absent", phase_name(sample->status.charge_state));
        set_label(s_metrics, line);
        snprintf(line, sizeof(line), "CV %u mV    ADC %02X\nDCDC %02X    LDO %02X    IRQ %02X/%02X/%02X",
                 sample->charger.voltage_mv, sample->adc, sample->dcdc, sample->ldo,
                 sample->irq.bank[0], sample->irq.bank[1], sample->irq.bank[2]);
        set_label(s_details, line);
    } else {
        set_label(s_current, "-- mA");
        set_label(s_metrics, esp_err_to_name(err));
        set_label(s_details, "");
    }
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
    set_label(s_start_label, s_confirm_until_us ? "Confirm " CHARGE_TEST_LABEL : CHARGE_TEST_LABEL " test");
    if (s_deadline_us) {
        const int64_t left_us = s_deadline_us - esp_timer_get_time();
        snprintf(line, sizeof(line), "Supervised test   %ld s", (long)((left_us + 999999) / 1000000));
        set_label(s_note, line);
    } else
#endif
    {
        set_label(s_note, s_message);
    }
    bsp_display_unlock();
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    i2c_master_bus_handle_t bus;
    esp_err_t err = candis_i2c_acquire(&bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus: %s", esp_err_to_name(err));
        return;
    }
    tg28_esp_config_t pmu_config = TG28_ESP_CONFIG_DEFAULT();
    pmu_config.timeout_ms = 30;
    err = tg28_esp_open(&s_pmu, bus, &pmu_config);
    if (err != ESP_OK) {
        (void)candis_i2c_release(bus);
        ESP_LOGE(TAG, "TG28 open: %s", esp_err_to_name(err));
        return;
    }
    uint8_t chip_id;
    err = tg28_esp_read_chip_id(&s_pmu, &chip_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TG28 CHIP_ID: %s", esp_err_to_name(err));
        if (tg28_esp_close(&s_pmu) == ESP_OK) {
            (void)candis_i2c_release(bus);
        }
        return;
    }
    ESP_LOGI(TAG, "TG28 CHIP_ID=0x%02X", chip_id);
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
    s_monitor = xTaskGetCurrentTaskHandle();
    s_test_locked = chip_id != TG28_ESP_CHIP_ID;
    s_message = s_test_locked ? "Charge test requires TG28-ESP" : "Supervised bench mode";
#endif
    candis_display_config_t display_config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_POWER_DEMO_PANEL_POWER
    display_config.manage_panel_power = true;
#endif
    lv_display_t *display;
    err = candis_display_start(&display_config, &display);
    if (err == ESP_OK) {
        if (bsp_display_lock(1000)) {
            create_ui(display);
            bsp_display_unlock();
            ESP_LOGI(TAG, "AMOLED power dashboard ready");
        } else {
            ESP_LOGW(TAG, "AMOLED dashboard lock timed out");
        }
    } else {
        ESP_LOGW(TAG, "AMOLED unavailable: %s", esp_err_to_name(err));
    }

    int64_t next_log = 0;
    for (;;) {
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
        uint32_t command = 0;
        xTaskNotifyWait(0, UINT32_MAX, &command, pdMS_TO_TICKS(250));
#else
        vTaskDelay(pdMS_TO_TICKS(250));
#endif
        sample_t sample;
        err = sample_power(&sample);
        const int64_t now = esp_timer_get_time();
#ifdef CONFIG_CANDIS_POWER_DEMO_CHARGE_TEST
        if (handle_charge(command, err, &sample, now)) {
            err = sample_power(&sample);
        }
#endif
        update_ui(err, &sample);
        if (now >= next_log) {
            next_log = now + 1000000;
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "VBUS=%u BAT=%u VBAT=%lu mV SOC=%d ICC_SET=%u mA CV=%u mV phase=%s "
                         "IIN=%u mA PRE=%u mA TERM=%u mA DCDC=%02x LDO=%02x ADC=%02x IRQ=%02x/%02x/%02x",
                         sample.status.vbus_good, sample.status.battery_present,
                         (unsigned long)(sample.battery_uv / 1000),
                         sample.soc_valid ? sample.soc : -1, sample.charger.current_ma,
                         sample.charger.voltage_mv, phase_name(sample.status.charge_state),
                         sample.charger.input_limit_ma, sample.charger.precharge_ma, sample.charger.termination_ma,
                         sample.dcdc, sample.ldo, sample.adc,
                         sample.irq.bank[0], sample.irq.bank[1], sample.irq.bank[2]);
            } else {
                ESP_LOGW(TAG, "PMU sample: %s", esp_err_to_name(err));
            }
        }
    }
}
