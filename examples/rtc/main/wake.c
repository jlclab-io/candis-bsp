/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include "bsp/candis.h"
#include "bsp/candis_rtc.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_sleep.h"
#include "esp_system.h"

#define WAKE_GPIO GPIO_NUM_0
#define WAKE_MASK (1ULL << WAKE_GPIO)
#define GUARD_US 5000000
#define TG28_ADDRESS 0x34
#define TG28_IRQ_ENABLE 0x40
#define TG28_DCDC_ENABLE 0x80
#define TG28_LDO_ENABLE 0x90
#define SLEEP_COOKIE 0x43414e44

static const char *TAG = "candis_rtc_wake";
static i2c_master_bus_handle_t s_pmu_bus;
static i2c_master_dev_handle_t s_pmu_dev;
static RTC_NOINIT_ATTR struct {
    uint32_t cookie;
    uint32_t inverted_cookie;
    bool restore_pmu;
    uint8_t pmu_irq[3];
    uint8_t power[3];
    candis_rtc_update_irq_t rtc;
} s_saved;

static struct {
    bool available;
    bool deep_sleep;
    bool ext1;
    bool guard;
    bool updated;
    esp_err_t restored;
} s_wake;

static esp_err_t pmu_release(void)
{
    if (s_pmu_dev) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_pmu_dev), TAG, "remove TG28 from I2C");
        s_pmu_dev = NULL;
    }
    if (s_pmu_bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release(s_pmu_bus), TAG, "release I2C");
        s_pmu_bus = NULL;
    }
    return ESP_OK;
}

static esp_err_t pmu_access(bool restore)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TG28_ADDRESS, .scl_speed_hz = 100000,
    };
    const uint8_t reg = TG28_IRQ_ENABLE;
    if (!s_pmu_bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_pmu_bus), TAG, "acquire I2C");
    }
    esp_err_t result = s_pmu_dev ? ESP_OK : i2c_master_bus_add_device(s_pmu_bus, &config, &s_pmu_dev);
    if (result == ESP_OK) {
        uint8_t packet[4] = {reg, 0, 0, 0};
        uint8_t power[3];
        const uint8_t dcdc = TG28_DCDC_ENABLE, ldo = TG28_LDO_ENABLE;
        result = i2c_master_transmit_receive(s_pmu_dev, &dcdc, 1, power, 1, 100);
        if (result == ESP_OK) {
            result = i2c_master_transmit_receive(s_pmu_dev, &ldo, 1, power + 1, 2, 100);
        }
        bool power_changed = false;
        if (restore) {
            power_changed = result != ESP_OK || memcmp(power, s_saved.power, sizeof(power));
            memcpy(packet + 1, s_saved.pmu_irq, sizeof(s_saved.pmu_irq));
            /* Restore the mask even if the power snapshot could not be read. */
            result = ESP_OK;
        } else if (result == ESP_OK) {
            memcpy(s_saved.power, power, sizeof(power));
            result = i2c_master_transmit_receive(s_pmu_dev, &reg, 1, s_saved.pmu_irq, sizeof(s_saved.pmu_irq), 100);
            if (result == ESP_OK) {
                s_saved.restore_pmu = true;
                printf("RTC_PMU_SAVED %02x/%02x/%02x power=%02x/%02x/%02x\n",
                       s_saved.pmu_irq[0], s_saved.pmu_irq[1], s_saved.pmu_irq[2], power[0], power[1], power[2]);
            }
        }
        if (result == ESP_OK) {
            result = i2c_master_transmit(s_pmu_dev, packet, sizeof(packet), 100);
        }
        uint8_t actual[3];
        if (result == ESP_OK) {
            result = i2c_master_transmit_receive(s_pmu_dev, &reg, 1, actual, sizeof(actual), 100);
            if (result == ESP_OK && memcmp(actual, packet + 1, sizeof(actual))) {
                result = ESP_ERR_INVALID_RESPONSE;
            }
        }
        if (result == ESP_OK && restore) {
            printf("RTC_PMU_RESTORED %02x/%02x/%02x\n", actual[0], actual[1], actual[2]);
            s_saved.restore_pmu = false;
            printf("RTC_POWER_UNCHANGED %d\n", !power_changed);
            if (power_changed) {
                result = ESP_ERR_INVALID_RESPONSE;
            }
        }
    }
    esp_err_t cleanup = pmu_release();
    return result == ESP_OK ? cleanup : result;
}

static esp_err_t restore_wake_sources(bool *updated)
{
    esp_err_t rtc = candis_rtc_disarm_update_irq(&s_saved.rtc, updated);
    esp_err_t pmu = s_saved.restore_pmu ? pmu_access(true) : pmu_release();
    if (rtc == ESP_OK && pmu == ESP_OK) {
        memset(&s_saved, 0, sizeof(s_saved));
    }
    return rtc == ESP_OK ? pmu : rtc;
}

void candis_rtc_example_resume(void)
{
    if (s_saved.cookie != SLEEP_COOKIE || s_saved.inverted_cookie != (uint32_t)~SLEEP_COOKIE) {
        memset(&s_saved, 0, sizeof(s_saved));
        return;
    }
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    s_wake.available = true;
    s_wake.deep_sleep = esp_reset_reason() == ESP_RST_DEEPSLEEP;
    s_wake.ext1 = (esp_sleep_get_ext1_wakeup_status() & WAKE_MASK) != 0;
    s_wake.guard = (causes & (1U << ESP_SLEEP_WAKEUP_TIMER)) != 0;
    s_wake.restored = restore_wake_sources(&s_wake.updated);
}

void candis_rtc_example_report_wake(void)
{
    if (!s_wake.available) {
        printf("RTC_WAKE NONE reset=%d\n", esp_reset_reason());
        return;
    }
    bool pass = s_wake.deep_sleep && s_wake.ext1 && !s_wake.guard && s_wake.updated && s_wake.restored == ESP_OK;
    printf("RTC_WAKE %s deep=%d ext1=%d guard=%d update=%d restore=%s\n",
           pass ? "ESP_OK" : "FAIL", s_wake.deep_sleep, s_wake.ext1, s_wake.guard,
           s_wake.updated, esp_err_to_name(s_wake.restored));
}

esp_err_t candis_rtc_example_sleep(void)
{
    if (s_saved.cookie == SLEEP_COOKIE) {
        esp_err_t result = restore_wake_sources(NULL);
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
    }
    struct tm utc;
    ESP_RETURN_ON_ERROR(candis_rtc_get_time(&utc), TAG, "read valid RTC before sleep");
    const gpio_config_t gpio = {.pin_bit_mask = WAKE_MASK, .mode = GPIO_MODE_INPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&gpio), TAG, "configure shared interrupt input");
    s_saved.cookie = SLEEP_COOKIE;
    s_saved.inverted_cookie = (uint32_t)~SLEEP_COOKIE;
    /* Isolate TG28 only for this test so GPIO0 wake can be attributed to the RTC. */
    s_wake.available = false;
    esp_err_t result = pmu_access(false);
    if (result == ESP_OK && gpio_get_level(WAKE_GPIO) != 1) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        result = esp_sleep_enable_ext1_wakeup_io(WAKE_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (result == ESP_OK) {
        result = esp_sleep_enable_timer_wakeup(GUARD_US);
    }
    if (result == ESP_OK) {
        puts("RTC_SLEEP_ARMED gpio=0 guard_ms=5000");
        fflush(stdout);
        result = candis_rtc_arm_update_irq(&s_saved.rtc);
    }
    if (result == ESP_OK) {
        result = esp_deep_sleep_try_to_start();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_sleep_disable_ext1_wakeup_io(WAKE_MASK));
    esp_err_t timer = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (timer != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(timer);
    }
    esp_err_t restore = restore_wake_sources(NULL);
    return restore == ESP_OK ? result : restore;
}
