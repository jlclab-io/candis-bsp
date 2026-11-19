/* SPDX-License-Identifier: Apache-2.0 */
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

static const char *TAG = "candis_otg";
static const tinyusb_cdcacm_itf_t PORT = TINYUSB_CDC_ACM_0;

static void echo_task(void *arg)
{
    uint8_t buffer[64];
    for (;;) {
        size_t count = 0;
        esp_err_t err = tinyusb_cdcacm_read(PORT, buffer, sizeof(buffer), &count);
        if (err == ESP_OK && count) {
            size_t queued = tinyusb_cdcacm_write_queue(PORT, buffer, count);
            err = tinyusb_cdcacm_write_flush(PORT, pdMS_TO_TICKS(100));
            if (queued != count || err != ESP_OK) {
                ESP_LOGW(TAG, "CDC echo incomplete: RX=%u TX=%u status=%s",
                         (unsigned)count, (unsigned)queued, esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t candis_diagnostics_start_otg(void)
{
    const tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&usb_config), TAG, "install USB device stack");
    const tinyusb_config_cdcacm_t cdc_config = {.cdc_port = PORT};
    esp_err_t err = tinyusb_cdcacm_init(&cdc_config);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_driver_uninstall());
        return err;
    }
    if (xTaskCreate(echo_task, "candis_otg", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_cdcacm_deinit(PORT));
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_driver_uninstall());
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTG CDC echo ready");
    return ESP_OK;
}
