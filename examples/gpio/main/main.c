/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_gpio";
static const gpio_num_t USER_LED = GPIO_NUM_37;

void app_main(void)
{
    ESP_ERROR_CHECK(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));

    // The LED and its pull-up allow open-drain operation on this shared pin.
    ESP_ERROR_CHECK(gpio_set_level(USER_LED, 1));
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << USER_LED,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    for (unsigned cycle = 0; ; ++cycle) {
        const bool on = cycle % 2 == 0;
        const int level = on ? 0 : 1;
        ESP_ERROR_CHECK(gpio_set_level(USER_LED, level));
        vTaskDelay(pdMS_TO_TICKS(10));
        const int actual = gpio_get_level(USER_LED);
        ESP_LOG_LEVEL(actual == level ? ESP_LOG_INFO : ESP_LOG_WARN, TAG,
                      "GPIO_LED gpio=%d state=%s pad=%d expected=%d", USER_LED,
                      on ? "on" : "off", actual, level);
        vTaskDelay(pdMS_TO_TICKS(990));
    }
}
