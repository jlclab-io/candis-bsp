/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdio.h>

#include "candis_example_usb.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_flash";

static void inspect_flash(void)
{
    uint32_t id;
    uint32_t size;
    esp_err_t err = esp_flash_read_id(NULL, &id);
    if (err == ESP_OK) {
        err = esp_flash_get_physical_size(NULL, &size);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "FLASH_CHIP jedec=0x%06" PRIx32 " physical_bytes=%" PRIu32, id, size);
    } else {
        ESP_LOGE(TAG, "FLASH_CHIP %s", esp_err_to_name(err));
    }
    const esp_partition_t *app = esp_ota_get_running_partition();
    if (!app) {
        ESP_LOGE(TAG, "FLASH_APP running partition unavailable");
        return;
    }
    uint8_t digest[32];
    err = esp_partition_get_sha256(app, digest);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FLASH_APP image check=%s", esp_err_to_name(err));
        return;
    }
    char hex[2 * sizeof(digest) + 1];
    for (unsigned i = 0; i < sizeof(digest); ++i) {
        snprintf(&hex[2 * i], 3, "%02x", digest[i]);
    }
    ESP_LOGI(TAG, "FLASH_APP label=%s offset=0x%" PRIx32 " bytes=%" PRIu32 " sha256=%s",
             app->label, app->address, app->size, hex);
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_LOGI(TAG, "FLASH_READY commands=I:inspect");
    inspect_flash();
    for (;;) {
        char byte;
        if (candis_example_usb_read(&byte) && (byte == 'I' || byte == 'i')) {
            inspect_flash();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
