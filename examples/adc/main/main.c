/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_power.h"
#include "candis_example_usb.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_adc";
#ifdef CONFIG_CANDIS_ADC_FIXTURE
static const int PINS[] = {43, 44};
#else
static const int PINS[] = {42, 43}; /* Dock LEFT U6.20 and U6.18. */
#endif
enum { CHANNELS = sizeof(PINS) / sizeof(PINS[0]), SAMPLES = 16, REPORTS = 12 };

static void sample_window(adc_oneshot_unit_handle_t adc, const adc_channel_t channels[CHANNELS],
                          adc_cali_handle_t cali)
{
    for (unsigned report = 0; report < REPORTS; ++report) {
        for (unsigned i = 0; i < CHANNELS; ++i) {
            int sum = 0;
            int minimum = 0;
            int maximum = 0;
            esp_err_t result = ESP_OK;
            for (unsigned sample = 0; sample < SAMPLES; ++sample) {
                int raw;
                result = adc_oneshot_read(adc, channels[i], &raw);
                if (result != ESP_OK) {
                    break;
                }
                sum += raw;
                if (sample == 0 || raw < minimum) {
                    minimum = raw;
                }
                if (sample == 0 || raw > maximum) {
                    maximum = raw;
                }
            }
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "ADC_ERROR gpio=%d read=%s", PINS[i], esp_err_to_name(result));
                continue;
            }
            const int average = sum / SAMPLES;
            int mv;
            if (cali && adc_cali_chan_raw_to_voltage(cali, channels[i], average, &mv) == ESP_OK) {
                ESP_LOGI(TAG, "ADC_SAMPLE gpio=%d raw=%d min=%d max=%d voltage=%d mV",
                         PINS[i], average, minimum, maximum, mv);
            } else {
                ESP_LOGI(TAG, "ADC_SAMPLE gpio=%d raw=%d min=%d max=%d voltage=unavailable",
                         PINS[i], average, minimum, maximum);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channels[CHANNELS];
    for (unsigned i = 0; i < CHANNELS; ++i) {
        adc_unit_t mapped_unit;
        ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PINS[i], &mapped_unit, &channels[i]));
        if (i == 0) {
            unit = mapped_unit;
        } else if (mapped_unit != unit) {
            ESP_LOGE(TAG, "ADC pins must use the same ADC unit");
            return;
        }
    }
    adc_oneshot_unit_handle_t adc;
    const adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = unit};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc));
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_0,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    for (unsigned i = 0; i < CHANNELS; ++i) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, channels[i], &channel_config));
    }
    const adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = channel_config.atten,
        .bitwidth = channel_config.bitwidth,
    };
    adc_cali_handle_t cali = NULL;
    const esp_err_t calibration = adc_cali_create_scheme_line_fitting(&cali_config, &cali);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "ADC_READY GPIO%d/%d samples=%d atten=0dB calibration=%s",
             PINS[0], PINS[1], SAMPLES, esp_err_to_name(calibration));
    for (;;) {
        if (candis_example_usb_begin_operation()) {
            esp_err_t power = ESP_OK;
#ifdef CONFIG_CANDIS_ADC_MANAGE_POWER
            power = candis_expansion_power_acquire(true);
#else
            candis_power_state_t state;
            power = candis_power_read(&state);
            if (power == ESP_OK) {
                ESP_LOGI(TAG, "ADC_POWER DC1SW=%s ALDO3=%s",
                         state.dc1sw ? "on" : "off", state.aldo3 ? "on" : "off");
            }
#endif
            if (power == ESP_OK) {
                sample_window(adc, channels, cali);
            } else {
                ESP_LOGW(TAG, "ADC_POWER %s", esp_err_to_name(power));
            }
#ifdef CONFIG_CANDIS_ADC_MANAGE_POWER
            // Keep the download guard active until DC1SW is restored.
            esp_err_t release;
            do {
                release = candis_expansion_power_release();
                ESP_LOG_LEVEL(release == ESP_OK ? ESP_LOG_INFO : ESP_LOG_WARN, TAG,
                              "ADC_RELEASE %s", esp_err_to_name(release));
                if (release != ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            } while (release != ESP_OK);
#endif
            candis_example_usb_end_operation();
        }
        ESP_LOGI(TAG, "ADC_IDLE send S to sample again");
        char command;
        do {
            vTaskDelay(pdMS_TO_TICKS(20));
        } while (!candis_example_usb_read(&command) || (command != 'S' && command != 's'));
    }
}
