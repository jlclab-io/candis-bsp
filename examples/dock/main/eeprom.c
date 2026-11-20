/* SPDX-License-Identifier: Apache-2.0 */
#include "driver/i2c_master.h"
#include "eeprom.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "dock_eeprom";
enum { EEPROM_BYTES = 256, EEPROM_ADDRESS = 0x50 };
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_eeprom;

esp_err_t dock_eeprom_release(void)
{
    if (s_eeprom) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_eeprom), TAG, "remove U10");
        s_eeprom = NULL;
    }
    if (!s_bus) {
        return ESP_OK;
    }
    const esp_err_t err = i2c_del_master_bus(s_bus);
    if (err == ESP_OK) {
        s_bus = NULL;
    }
    return err;
}

static esp_err_t open_eeprom(void)
{
    ESP_RETURN_ON_ERROR(dock_eeprom_release(), TAG, "release previous U10 transport");
    /* Use the expansion I2C bus after the synchronous BSP probe has released it. */
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_1, .scl_io_num = GPIO_NUM_58, .sda_io_num = GPIO_NUM_59,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_bus), TAG, "allocate expansion bus");
    const i2c_device_config_t eeprom = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = EEPROM_ADDRESS, .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(s_bus, &eeprom, &s_eeprom);
}

static esp_err_t read_eeprom(uint8_t image[EEPROM_BYTES])
{
    const uint8_t address = 0;
    return i2c_master_transmit_receive(s_eeprom, &address, 1, image, EEPROM_BYTES, 100);
}

esp_err_t dock_eeprom_inspect(void)
{
    uint8_t image[EEPROM_BYTES];
    esp_err_t err = open_eeprom();
    if (err == ESP_OK) {
        err = read_eeprom(image);
    }
    const esp_err_t release = dock_eeprom_release();
    if (err == ESP_OK) {
        unsigned erased = 0;
        for (unsigned i = 0; i < EEPROM_BYTES; ++i) {
            erased += image[i] == UINT8_MAX;
        }
        ESP_LOGI(TAG, "EEPROM 0x50 bytes=%d erased_bytes=%u", EEPROM_BYTES, erased);
    }
    return err == ESP_OK ? release : err;
}
