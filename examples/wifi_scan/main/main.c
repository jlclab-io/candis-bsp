/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "candis_example_usb.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_wifi_scan";

static esp_err_t print_ap_records(void)
{
    wifi_ap_record_t records[8];
    uint16_t total;
    esp_err_t err = esp_wifi_scan_get_ap_num(&total);
    if (err != ESP_OK) {
        return err;
    }
    uint16_t count = sizeof(records) / sizeof(records[0]);
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        return err;
    }
    for (uint16_t i = 0; i < count; ++i) {
        const wifi_ap_record_t *ap = &records[i];
        char ssid[33];
        memcpy(ssid, ap->ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
        for (char *c = ssid; *c; ++c) {
            if ((unsigned char)*c < 0x20 || *c == 0x7f) {
                *c = '.';
            }
        }
        ESP_LOGI(TAG, "WIFI_AP ssid=\"%s\" bssid=" MACSTR " channel=%u rssi=%d dBm auth=%d",
                 ssid, MAC2STR(ap->bssid), ap->primary, ap->rssi, ap->authmode);
    }
    ESP_LOGI(TAG, "WIFI_DONE found=%u shown=%u", total, count);
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    const wifi_scan_config_t scan = {
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 250,
    };
    ESP_LOGI(TAG, "WIFI_READY passive scan, send S to repeat");
    vTaskDelay(pdMS_TO_TICKS(1500));
    for (;;) {
        ESP_LOGI(TAG, "WIFI_SCAN");
        esp_err_t err = esp_wifi_scan_start(&scan, true);
        if (err == ESP_OK) {
            err = print_ap_records();
        }
        if (err != ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_clear_ap_list());
            ESP_LOGE(TAG, "WIFI_ERROR %s", esp_err_to_name(err));
        }
        char command;
        do {
            vTaskDelay(pdMS_TO_TICKS(20));
        } while (!candis_example_usb_read(&command) || (command != 'S' && command != 's'));
    }
}
