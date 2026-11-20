/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_baseboard.h"
#include "bsp/candis_power.h"
#include "candis_example_usb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_baseboard";

static void probe(void)
{
    candis_power_state_t power;
    esp_err_t err = candis_power_read(&power);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BASEBOARD_POWER %s", esp_err_to_name(err));
        return;
    }
    if (!power.dc1sw) {
        ESP_LOGW(TAG, "BASEBOARD_SKIP DC1SW=off; supply the verified expansion domain before probing");
        return;
    }
    candis_baseboard_info_t board;
    err = candis_baseboard_probe(&board);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BASEBOARD_ID name=%s kind=%d hw_revision=%u format=%u",
                 board.name, board.kind, board.hardware_revision, board.format_revision);
    } else {
        ESP_LOGW(TAG, "BASEBOARD_PROBE %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_LOGI(TAG, "BASEBOARD_READY EEPROM=0x50 commands=P:probe");
    probe();
    for (;;) {
        char byte;
        if (candis_example_usb_read(&byte) && (byte == 'P' || byte == 'p')) {
            probe();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
