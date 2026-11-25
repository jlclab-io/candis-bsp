/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdbool.h>

#include "bsp/esp-bsp.h"
#include "candis_example_usb.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lp_core_i2c.h"
#include "lp_core_main.h"
#include "tg28_esp.h"
#include "ulp_lp_core.h"

extern const uint8_t lp_core_main_bin_start[] asm("_binary_lp_core_main_bin_start");
extern const uint8_t lp_core_main_bin_end[] asm("_binary_lp_core_main_bin_end");

static const char *TAG = "candis_lp_i2c";
#define GUARD_US (UINT64_C(20) * UINT64_C(1000000))

static esp_err_t hp_read_tg28(uint8_t *chip_id)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "initialize HP I2C");
    tg28_esp_t pmu = {0};
    esp_err_t err = tg28_esp_open(&pmu, bsp_i2c_get_handle(),
                                  &(tg28_esp_config_t)TG28_ESP_CONFIG_DEFAULT());
    if (err == ESP_OK) {
        err = tg28_esp_read_chip_id(&pmu, chip_id);
    }
    const esp_err_t close = tg28_esp_close(&pmu);
    const esp_err_t release = bsp_i2c_deinit();
    if (err == ESP_OK) {
        err = close;
    }
    if (err == ESP_OK) {
        err = release;
    }
    return err;
}

static esp_err_t start_lp_i2c(void)
{
    const lp_core_i2c_cfg_t config = {
        .i2c_pin_cfg = {
            .sda_io_num = GPIO_NUM_7,
            .scl_io_num = GPIO_NUM_6,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
        },
        .i2c_timing_cfg = {.clk_speed_hz = 400000},
        .i2c_src_clk = LP_I2C_SCLK_LP_FAST,
    };
    return lp_core_i2c_master_init(LP_I2C_NUM_0, &config);
}

static void load_lp_core(void)
{
    ESP_ERROR_CHECK(ulp_lp_core_load_binary(
        lp_core_main_bin_start,
        (size_t)(lp_core_main_bin_end - lp_core_main_bin_start)));
}

static void run_lp_core(void)
{
    ulp_lp_core_cfg_t config = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU,
    };
    ESP_ERROR_CHECK(ulp_lp_core_run(&config));
}

static esp_err_t release_lp_i2c_for_hp(void)
{
    ulp_lp_core_stop();
    ESP_RETURN_ON_ERROR(rtc_gpio_deinit(GPIO_NUM_6), TAG, "release LP I2C SCL");
    ESP_RETURN_ON_ERROR(rtc_gpio_deinit(GPIO_NUM_7), TAG, "release LP I2C SDA");
    return ESP_OK;
}

void app_main(void)
{
    const esp_reset_reason_t reset = esp_reset_reason();
    const uint32_t causes = esp_sleep_get_wakeup_causes();
    const bool ulp_wake = reset == ESP_RST_DEEPSLEEP &&
                          (causes & (UINT32_C(1) << ESP_SLEEP_WAKEUP_ULP)) != 0;
    const bool timer_wake = (causes & (UINT32_C(1) << ESP_SLEEP_WAKEUP_TIMER)) != 0;

    if (reset == ESP_RST_DEEPSLEEP) {
        const esp_err_t release = release_lp_i2c_for_hp();
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
        uint8_t hp_chip_id = 0;
        const esp_err_t hp_err = hp_read_tg28(&hp_chip_id);
        const bool lp_ok = ulp_lp_tg28_transaction_ok && ulp_lp_tg28_chip_id == 0x4A;
        const bool hp_ok = hp_err == ESP_OK && hp_chip_id == 0x4A;
        const bool pass = release == ESP_OK && ulp_wake && !timer_wake && lp_ok && hp_ok;

        ESP_LOGI(TAG, "WAKE causes=0x%08" PRIx32 " ulp=%d timer=%d",
                 causes, ulp_wake, timer_wake);
        ESP_LOGI(TAG, "LP_RELEASE %s", esp_err_to_name(release));
        ESP_LOGI(TAG, "LP_RESULT id=0x%02" PRIx32 " attempts=%" PRIu32
                      " error=%" PRId32 " ok=%d",
                 ulp_lp_tg28_chip_id, ulp_lp_tg28_attempts,
                 ulp_lp_tg28_last_error, ulp_lp_tg28_transaction_ok);
        ESP_LOGI(TAG, "HP_RESULT id=0x%02x error=%s", hp_chip_id, esp_err_to_name(hp_err));
        ESP_LOGI(TAG, "LP_I2C_RESULT %s", pass ? "PASS" : "FAIL");
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGI(TAG, "LP_I2C_RESULT %s lp=0x%02" PRIx32 " hp=0x%02x",
                     pass ? "PASS" : "FAIL", ulp_lp_tg28_chip_id, hp_chip_id);
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    uint8_t hp_chip_id = 0;
    ESP_ERROR_CHECK(hp_read_tg28(&hp_chip_id));
    ESP_LOGI(TAG, "HP_BEFORE id=0x%02x; releasing GPIO6/7 for LP", hp_chip_id);
    ESP_ERROR_CHECK(start_lp_i2c());
    load_lp_core();
    ESP_LOGI(TAG, "LP_I2C_READY scl=GPIO6 sda=GPIO7");

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(GUARD_US));
    ESP_ERROR_CHECK(candis_example_usb_stop());
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "ENTER_DEEP_SLEEP");
    run_lp_core();
    esp_deep_sleep_start();
}
