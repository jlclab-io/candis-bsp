/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "candis_example_usb.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tg28_esp.h"

static const char *TAG = "candis_sleep";
enum { SLEEP_SECONDS = 4 };
RTC_NOINIT_ATTR static uint32_t s_cycles;
static const uint64_t TIMER_US = SLEEP_SECONDS * UINT64_C(1000000);

#define SYSTEM_IRQ_GPIO GPIO_NUM_0
#define SYSTEM_IRQ_MASK (UINT64_C(1) << SYSTEM_IRQ_GPIO)
#define PWRON_GUARD_US (UINT64_C(15) * UINT64_C(1000000))
#define PWRON_COOKIE UINT32_C(0x50575231)
#define PWRON_LONG_BANK1_BIT (UINT8_C(1) << 2)
#define PWRON_SHORT_BANK1_BIT (UINT8_C(1) << 3)
#define PWRON_BANK1_MASK (PWRON_LONG_BANK1_BIT | PWRON_SHORT_BANK1_BIT)

RTC_NOINIT_ATTR static struct {
    uint32_t cookie;
    tg28_esp_irq_t original_enable;
} s_pwron;

static esp_err_t open_pmu(tg28_esp_t *pmu)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "open main I2C");
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_i2c_deinit());
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = tg28_esp_open(pmu, bus, &(tg28_esp_config_t)TG28_ESP_CONFIG_DEFAULT());
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_i2c_deinit());
    }
    return err;
}

static esp_err_t close_pmu(tg28_esp_t *pmu)
{
    const esp_err_t close = tg28_esp_close(pmu);
    const esp_err_t release = bsp_i2c_deinit();
    return close == ESP_OK ? release : close;
}

static bool woke_by_timer(void)
{
    return (esp_sleep_get_wakeup_causes() & (UINT32_C(1) << ESP_SLEEP_WAKEUP_TIMER)) != 0;
}

static bool woke_by_system_irq(void)
{
    return (esp_sleep_get_ext1_wakeup_status() & SYSTEM_IRQ_MASK) != 0;
}

static esp_err_t wait_system_irq_high(void)
{
    for (unsigned stable = 0, tries = 0; tries < 100 && stable < 20; ++tries) {
        stable = gpio_get_level(SYSTEM_IRQ_GPIO) ? stable + 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return gpio_get_level(SYSTEM_IRQ_GPIO) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void report_pwron_wake(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP || s_pwron.cookie != PWRON_COOKIE) {
        return;
    }

    tg28_esp_t pmu = {0};
    esp_err_t err = open_pmu(&pmu);
    uint8_t chip_id = 0;
    tg28_esp_irq_t status = {0};
    if (err == ESP_OK) {
        err = tg28_esp_read_chip_id(&pmu, &chip_id);
    }
    if (err == ESP_OK) {
        err = tg28_esp_read_irq(&pmu, &status);
    }
    const bool short_press = (status.bank[1] & PWRON_SHORT_BANK1_BIT) != 0;
    const bool long_press = (status.bank[1] & PWRON_LONG_BANK1_BIT) != 0;
    const bool ext1 = woke_by_system_irq();
    const bool guard = woke_by_timer();

    if (err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(tg28_esp_set_irq_enable(&pmu, &s_pwron.original_enable));
        ESP_ERROR_CHECK_WITHOUT_ABORT(tg28_esp_clear_irq(&pmu, &status));
    }
    if (err == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(close_pmu(&pmu));
    }
    memset(&s_pwron, 0, sizeof(s_pwron));

    const bool pass = err == ESP_OK && ext1 && !guard && (short_press || long_press);
    ESP_LOGI(TAG, "TG28_IRQ_STATUS bank=0x%02x/0x%02x/0x%02x short=%d long=%d",
             status.bank[0], status.bank[1], status.bank[2], short_press, long_press);
    ESP_LOGI(TAG, "PWRON_WAKE_RESULT %s chip=0x%02x ext1=%d guard=%d",
             pass ? "PASS" : "FAIL", chip_id, ext1, guard);
}

static esp_err_t pwron_sleep_once(void)
{
    if (!candis_example_usb_begin_operation()) {
        return ESP_ERR_INVALID_STATE;
    }

    tg28_esp_t pmu = {0};
    tg28_esp_irq_t current = {0};
    esp_err_t err = open_pmu(&pmu);
    if (err == ESP_OK) {
        err = tg28_esp_read_irq_enable(&pmu, &current);
    }
    const tg28_esp_irq_t pwron_only = {.bank = {0, PWRON_BANK1_MASK, 0}};
    if (err == ESP_OK) {
        err = tg28_esp_set_irq_enable(&pmu, &pwron_only);
    }
    tg28_esp_irq_t readback = {0};
    if (err == ESP_OK) {
        err = tg28_esp_read_irq_enable(&pmu, &readback);
    }
    if (err == ESP_OK && memcmp(&pwron_only, &readback, sizeof(readback)) != 0) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    tg28_esp_irq_t pending = {0};
    if (err == ESP_OK) {
        err = tg28_esp_read_irq(&pmu, &pending);
    }
    if (err == ESP_OK) {
        err = tg28_esp_clear_irq(&pmu, &pending);
    }

    const gpio_config_t input = {
        .pin_bit_mask = SYSTEM_IRQ_MASK,
        .mode = GPIO_MODE_INPUT,
    };
    if (err == ESP_OK) {
        err = gpio_config(&input);
    }
    if (err == ESP_OK) {
        err = wait_system_irq_high();
    }
    if (err == ESP_OK) {
        err = esp_sleep_enable_ext1_wakeup_io(SYSTEM_IRQ_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (err == ESP_OK) {
        err = esp_sleep_enable_timer_wakeup(PWRON_GUARD_US);
    }

    if (err == ESP_OK) {
        s_pwron.original_enable = current;
        s_pwron.cookie = PWRON_COOKIE;
        ESP_LOGI(TAG, "PWRON_SLEEP_ARMED gpio=0 enable=0x%02x/0x%02x/0x%02x guard_s=15",
                 readback.bank[0], readback.bank[1], readback.bank[2]);
        ESP_ERROR_CHECK_WITHOUT_ABORT(close_pmu(&pmu));
        vTaskDelay(pdMS_TO_TICKS(200));
        err = candis_example_usb_stop();
        if (err == ESP_OK) {
            esp_deep_sleep_start();
        }
    } else {
        if (current.bank[0] || current.bank[1] || current.bank[2]) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(tg28_esp_set_irq_enable(&pmu, &current));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(close_pmu(&pmu));
    }

    candis_example_usb_end_operation();
    ESP_LOGE(TAG, "PWRON_SLEEP_ERROR %s", esp_err_to_name(err));
    return err;
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
    ESP_LOGI(TAG, "SLEEP_READY deep_reset=%d timer=%d ext1=%d deep_cycles=%" PRIu32 " commands=L,D,P,?",
             esp_reset_reason() == ESP_RST_DEEPSLEEP,
             woke_by_timer(), woke_by_system_irq(), s_cycles);
}

void app_main(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        s_cycles = 0;
        memset(&s_pwron, 0, sizeof(s_pwron));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1000));
    report_pwron_wake();
    report_ready();
    for (;;) {
        char byte;
        if (candis_example_usb_read(&byte)) {
            if (byte == 'L' || byte == 'D') {
                sleep_once(byte == 'D');
            } else if (byte == 'P') {
                ESP_ERROR_CHECK_WITHOUT_ABORT(pwron_sleep_once());
            } else if (byte == '?') {
                report_ready();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
