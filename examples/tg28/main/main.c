/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tg28_esp.h"

static const char *TAG = "tg28_monitor";

static esp_err_t show_configuration(const tg28_esp_t *pmu)
{
    uint8_t id, on, off;
    uint16_t seconds;
    bool watchdog;
    tg28_esp_watchdog_action_t action;
    tg28_esp_gauge_config_t gauge;
    tg28_esp_irq_t irq_enable;
    ESP_RETURN_ON_ERROR(tg28_esp_read_chip_id(pmu, &id), TAG, "REG03");
    ESP_RETURN_ON_ERROR(tg28_esp_read_power_sources(pmu, &on, &off), TAG, "power reasons");
    ESP_RETURN_ON_ERROR(tg28_esp_read_watchdog(pmu, &watchdog, &seconds, &action), TAG, "watchdog");
    ESP_RETURN_ON_ERROR(tg28_esp_read_gauge_config(pmu, &gauge), TAG, "fuel gauge");
    ESP_RETURN_ON_ERROR(tg28_esp_read_irq_enable(pmu, &irq_enable), TAG, "IRQ enable");
    ESP_LOGI(TAG, "id=0x%02x on=0x%02x off=0x%02x watchdog=%u/%us/%d gauge=%u sram=%u irq_mask=%02x:%02x:%02x",
             id, on, off, watchdog, seconds, action, gauge.enabled, gauge.sram_selected,
             irq_enable.bank[2], irq_enable.bank[1], irq_enable.bank[0]);

    for (tg28_esp_rail_t rail = TG28_ESP_RAIL_DCDC1; rail <= TG28_ESP_RAIL_DC4SW; ++rail) {
        bool enabled;
        uint16_t mv;
        ESP_RETURN_ON_ERROR(tg28_esp_read_rail_enabled(pmu, rail, &enabled), TAG, "rail enable");
        const esp_err_t err = tg28_esp_read_rail_voltage(pmu, rail, &mv);
        if (err == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG, "rail=%d enabled=%u setpoint uses a reserved encoding", rail, enabled);
        } else {
            ESP_RETURN_ON_ERROR(err, TAG, "rail voltage");
            ESP_LOGI(TAG, "rail=%d enabled=%u setpoint=%u mV", rail, enabled, mv);
        }
    }
    return ESP_OK;
}

static void capture_events(uint32_t events, void *user)
{
    *(uint32_t *)user = events;
}

static esp_err_t sample(const tg28_esp_t *pmu)
{
    tg28_esp_status_t status;
    tg28_esp_charger_t charger;
    uint8_t adc;
    uint32_t events = 0;
    ESP_RETURN_ON_ERROR(tg28_esp_read_status(pmu, &status), TAG, "status");
    ESP_RETURN_ON_ERROR(tg28_esp_read_charger(pmu, &charger), TAG, "charger settings");
    ESP_RETURN_ON_ERROR(tg28_esp_read_adc_mask(pmu, &adc), TAG, "ADC enable");
    ESP_RETURN_ON_ERROR(tg28_esp_dispatch_irq(pmu, TG28_ESP_IRQ_ALL, capture_events, &events), TAG, "IRQ status");
    ESP_LOGI(TAG, "vbus=%u cell_present=%u system_on=%u phase=%d CC_setpoint=%u mA CV_setpoint=%u mV irq=%06" PRIx32,
             status.vbus_good, status.battery_present, status.system_on, status.charge_state,
             charger.current_ma, charger.voltage_mv, events);
    ESP_LOGI(TAG, "charger_enabled=%u battery_detection=%u batfet_open=%u battery_direction=%d adc_mask=0x%02x",
             charger.enabled, charger.detection_enabled, status.batfet_open, status.direction, adc);

    if (status.battery_present) {
        uint8_t soc;
        const esp_err_t err = tg28_esp_read_soc(pmu, &soc);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "fuel gauge disabled");
        } else {
            ESP_RETURN_ON_ERROR(err, TAG, "SOC");
            ESP_LOGI(TAG, "SOC=%u%%", soc);
        }
    }
    for (uint8_t channel = TG28_ESP_ADC_VBAT; channel <= TG28_ESP_ADC_DIE; channel <<= 1) {
        if (!(adc & channel)) {
            continue;
        }
        if (channel == TG28_ESP_ADC_VBAT && !status.battery_present) {
            uint16_t raw;
            ESP_RETURN_ON_ERROR(tg28_esp_read_adc_raw(pmu, channel, &raw), TAG, "VBAT ADC");
            ESP_LOGI(TAG, "VBAT ADC raw=0x%04x (battery absent)", raw);
            continue;
        }
        uint32_t uv;
        const esp_err_t err = tg28_esp_read_adc_uv(pmu, channel, &uv);
        if (channel == TG28_ESP_ADC_TS && err == ESP_ERR_INVALID_RESPONSE) {
            ESP_LOGW(TAG, "TS ADC overflow");
        } else {
            ESP_RETURN_ON_ERROR(err, TAG, "ADC data");
            ESP_LOGI(TAG, "ADC channel=0x%02x input=%" PRIu32 " uV", channel, uv);
        }
    }
    return ESP_OK;
}

#ifdef CONFIG_TG28_EXAMPLE_SCRATCH_TEST
static esp_err_t test_scratch(const tg28_esp_t *pmu)
{
    uint8_t original[4], pattern[4], actual[4];
    ESP_RETURN_ON_ERROR(tg28_esp_read_buffer(pmu, original), TAG, "save scratch");
    for (size_t i = 0; i < sizeof(pattern); ++i) {
        pattern[i] = original[i] ^ 0xff;
    }
    esp_err_t err = tg28_esp_write_buffer(pmu, pattern);
    if (err == ESP_OK) {
        err = tg28_esp_read_buffer(pmu, actual);
    }
    if (err == ESP_OK && memcmp(actual, pattern, sizeof(actual)) != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    /* A failed transfer can have reached the PMU. Always restore and read back. */
    ESP_RETURN_ON_ERROR(tg28_esp_write_buffer(pmu, original), TAG, "restore scratch");
    ESP_RETURN_ON_ERROR(tg28_esp_read_buffer(pmu, actual), TAG, "verify scratch restore");
    ESP_RETURN_ON_FALSE(memcmp(actual, original, sizeof(actual)) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG, "scratch restore mismatch");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "scratch pattern verified and original bytes restored");
    }
    return err;
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_TG28_EXAMPLE_SCL,
        .sda_io_num = CONFIG_TG28_EXAMPLE_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "create bus: %s", esp_err_to_name(err));
        return;
    }
    tg28_esp_t pmu = {0};
    const tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    err = tg28_esp_open(&pmu, bus, &config);
    if (err == ESP_OK) {
        err = tg28_esp_check_chip_id(&pmu);
    }
    if (err == ESP_OK) {
        err = show_configuration(&pmu);
    }
#ifdef CONFIG_TG28_EXAMPLE_SCRATCH_TEST
    if (err == ESP_OK) {
        err = test_scratch(&pmu);
    }
#endif
    unsigned samples = 0;
    while (err == ESP_OK && samples < CONFIG_TG28_EXAMPLE_SAMPLES) {
        if (samples) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        err = sample(&pmu);
        if (err == ESP_OK) {
            ++samples;
        }
    }
    const esp_err_t remove = tg28_esp_close(&pmu);
    const esp_err_t release = remove == ESP_OK ? i2c_del_master_bus(bus) : remove;
    ESP_LOGI(TAG, "samples=%u monitor=%s cleanup=%s", samples, esp_err_to_name(err), esp_err_to_name(release));
}
