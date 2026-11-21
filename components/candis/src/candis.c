/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2. GPIO6/7 are shared with the LP I2C controller. */
#include <stdatomic.h>
#include "candis_internal.h"
#include "freertos/FreeRTOS.h"

static atomic_flag s_control = ATOMIC_FLAG_INIT;
static i2c_master_bus_handle_t s_bus;
static unsigned s_users;
static unsigned s_external_users;
static i2c_master_bus_handle_t s_bsp_bus;

bool candis_control_enter(void)
{
    return !xPortInIsrContext() && !atomic_flag_test_and_set(&s_control);
}

void candis_control_leave(void)
{
    atomic_flag_clear(&s_control);
}

esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus)
{
    if (s_users == 0) {
        const i2c_master_bus_config_t config = {
            .i2c_port = CANDIS_MAIN_I2C,
            .sda_io_num = CANDIS_MAIN_SDA,
            .scl_io_num = CANDIS_MAIN_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
        };
        esp_err_t err = i2c_new_master_bus(&config, &s_bus);
        if (err != ESP_OK) {
            return err;
        }
    }
    ++s_users;
    *bus = s_bus;
    return ESP_OK;
}

esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus)
{
    if (!bus || bus != s_bus || s_users == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_users == 1) {
        esp_err_t err = i2c_del_master_bus(s_bus);
        if (err != ESP_OK) {
            return err;
        }
        s_bus = NULL;
    }
    --s_users;
    return ESP_OK;
}

esp_err_t candis_i2c_acquire(i2c_master_bus_handle_t *bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    *bus = NULL;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = candis_i2c_acquire_locked(bus);
    if (err == ESP_OK) {
        ++s_external_users;
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_i2c_release(i2c_master_bus_handle_t bus)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_external_users ? candis_i2c_release_locked(bus) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        --s_external_users;
    }
    candis_control_leave();
    return err;
}

static esp_err_t bsp_i2c_init_locked(void)
{
    return s_bsp_bus ? ESP_OK : candis_i2c_acquire_locked(&s_bsp_bus);
}

esp_err_t bsp_i2c_init(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = bsp_i2c_init_locked();
    candis_control_leave();
    return err;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    if (!candis_control_enter()) {
        return NULL;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_init_locked() == ESP_OK ? s_bsp_bus : NULL;
    candis_control_leave();
    return bus;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_bsp_bus ? candis_i2c_release_locked(s_bsp_bus) : ESP_OK;
    if (err == ESP_OK) {
        s_bsp_bus = NULL;
    }
    candis_control_leave();
    return err;
}
