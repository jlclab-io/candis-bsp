/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>

#include "candis_example_usb.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_sleep";
enum { SLEEP_SECONDS = 4 };
RTC_NOINIT_ATTR static uint32_t s_cycles;
static const uint64_t TIMER_US = SLEEP_SECONDS * UINT64_C(1000000);

static bool woke_by_timer(void)
{
    return (esp_sleep_get_wakeup_causes() & (UINT32_C(1) << ESP_SLEEP_WAKEUP_TIMER)) != 0;
}

static void sleep_once(bool deep)
{
    if (!candis_example_usb_begin_operation()) {
        return;
    }
    esp_err_t err = esp_sleep_enable_timer_wakeup(TIMER_US);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SLEEP_ENTER mode=%s seconds=%d", deep ? "deep" : "light", SLEEP_SECONDS);
        vTaskDelay(pdMS_TO_TICKS(250));
        err = candis_example_usb_stop();
        if (err == ESP_OK) {
            if (deep) {
                ++s_cycles;
                esp_deep_sleep_start();
            }
            const int64_t start = esp_timer_get_time();
            err = esp_light_sleep_start();
            const int64_t elapsed = esp_timer_get_time() - start;
            ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "LIGHT_WAKE timer=%d elapsed_ms=%" PRId64 " result=%s",
                     woke_by_timer(),
                     elapsed / 1000, esp_err_to_name(err));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER));
    }
    candis_example_usb_end_operation();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SLEEP_ERROR %s", esp_err_to_name(err));
    }
}

static void report_ready(void)
{
    ESP_LOGI(TAG, "SLEEP_READY deep_reset=%d timer=%d deep_cycles=%" PRIu32 " commands=L,D,?",
             esp_reset_reason() == ESP_RST_DEEPSLEEP,
             woke_by_timer(), s_cycles);
}

void app_main(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        s_cycles = 0;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1000));
    report_ready();
    for (;;) {
        char byte;
        if (candis_example_usb_read(&byte)) {
            if (byte == 'L' || byte == 'D') {
                sleep_once(byte == 'D');
            } else if (byte == '?') {
                report_ready();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
