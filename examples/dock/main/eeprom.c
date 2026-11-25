/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_dock.h"
#include "eeprom.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "dock_eeprom";
enum { EEPROM_BYTES = 256, EEPROM_ADDRESS = 0x50 };

esp_err_t dock_eeprom_release(void)
{
    return ESP_OK;
}

esp_err_t dock_eeprom_inspect(void)
{
    uint8_t image[EEPROM_BYTES];
    const esp_err_t err = candis_dock_read_eeprom(image, sizeof(image));
    if (err == ESP_OK) {
        unsigned erased = 0;
        for (unsigned i = 0; i < EEPROM_BYTES; ++i) {
            erased += image[i] == UINT8_MAX;
        }
        ESP_LOGI(TAG, "EEPROM 0x50 bytes=%d erased_bytes=%u", EEPROM_BYTES, erased);
    }
    return err;
}
