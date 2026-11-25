/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/esp-bsp.h"
#include "candis_example_usb.h"
#include "esp_log.h"

static const char *TAG = "candis_minimal";

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));

    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_i2c_init());
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_ERROR_CHECK(bus ? ESP_OK : ESP_ERR_INVALID_STATE);

    i2c_master_bus_handle_t borrower;
    ESP_ERROR_CHECK(candis_i2c_acquire(&borrower));
    ESP_ERROR_CHECK(bus == borrower ? ESP_OK : ESP_ERR_INVALID_STATE);
    ESP_ERROR_CHECK(bsp_i2c_deinit());
    ESP_ERROR_CHECK(candis_i2c_release(borrower));

    /* Adding a device registers software state without sending a transaction. */
    ESP_ERROR_CHECK(bsp_i2c_init());
    bus = bsp_i2c_get_handle();
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x34,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t pending;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &pending));
    const esp_err_t occupied = bsp_i2c_deinit();
    ESP_ERROR_CHECK(occupied == ESP_ERR_INVALID_STATE ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(pending));
    ESP_ERROR_CHECK(bsp_i2c_deinit());
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_i2c_deinit());
    ESP_LOGI(TAG, "CANDIS_I2C_LIFECYCLE_OK");
}
