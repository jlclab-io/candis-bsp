/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <string.h>
#include "bsp/candis_baseboard.h"
#include "bsp/candis_baseboard_bus.h"
#include "esp_check.h"

static const char *TAG = "candis_baseboard";

esp_err_t candis_baseboard_probe_on_bus(i2c_master_bus_handle_t bus, uint8_t address,
                                        candis_baseboard_info_t *info)
{
    if (!bus || !info || address < 0x08 || address > 0x77) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, address, 30), TAG, "probe descriptor");
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t eeprom;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device, &eeprom),
                        TAG, "add descriptor device");
    uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE];
    const uint8_t offset = 0;
    const esp_err_t read = i2c_master_transmit_receive(eeprom, &offset, sizeof(offset),
                                                       image, sizeof(image), 40);
    const esp_err_t remove = i2c_master_bus_rm_device(eeprom);
    if (read != ESP_OK) {
        return read;
    }
    ESP_RETURN_ON_ERROR(remove, TAG, "remove descriptor device");
    return candis_baseboard_decode(image, sizeof(image), info);
}

esp_err_t candis_baseboard_probe_state_on_bus(candis_baseboard_kind_t expected,
                                              i2c_master_bus_handle_t bus, uint8_t address,
                                              candis_capability_state_t *state,
                                              candis_baseboard_info_t *info)
{
    if (!state || !bus || address < 0x08 || address > 0x77) {
        return ESP_ERR_INVALID_ARG;
    }
    if (expected != CANDIS_BASEBOARD_BB) {
        *state = CANDIS_CAPABILITY_UNSUPPORTED;
        return ESP_OK;
    }
    const esp_err_t present = i2c_master_probe(bus, address, 40);
    if (present == ESP_ERR_NOT_FOUND || present == ESP_ERR_TIMEOUT ||
        present == ESP_ERR_INVALID_RESPONSE) {
        *state = CANDIS_CAPABILITY_NOT_PRESENT;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(present, TAG, "probe descriptor address");
    candis_baseboard_info_t detected;
    const esp_err_t err = candis_baseboard_probe_on_bus(bus, address, &detected);
    if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT ||
        err == ESP_ERR_INVALID_RESPONSE) {
        *state = CANDIS_CAPABILITY_NOT_PRESENT;
        return ESP_OK;
    }
    if (err == ESP_ERR_NOT_SUPPORTED || err == ESP_ERR_INVALID_CRC ||
        err == ESP_ERR_INVALID_SIZE) {
        *state = CANDIS_CAPABILITY_UNSUPPORTED;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "probe descriptor");
    if (info) {
        *info = detected;
    }
    *state = CANDIS_CAPABILITY_PRESENT;
    return ESP_OK;
}

esp_err_t candis_i2c_probe_addresses(i2c_master_bus_handle_t bus,
                                     const uint8_t *addresses, size_t count,
                                     uint32_t *responding)
{
    if (!bus || !addresses || !count || count > 32 || !responding) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t mask = 0;
    for (size_t i = 0; i < count; ++i) {
        if (addresses[i] < 0x08 || addresses[i] > 0x77) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const esp_err_t err = i2c_master_probe(bus, addresses[i], 30);
        if (err == ESP_OK) {
            mask |= UINT32_C(1) << i;
        } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
            ESP_RETURN_ON_ERROR(err, TAG, "probe address 0x%02x", addresses[i]);
        }
    }
    *responding = mask;
    return ESP_OK;
}
