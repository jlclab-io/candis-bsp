/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2. TG28-ESP register table, REG90 and REG92/93. */
#include "bsp/candis_power.h"
#include "candis_internal.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TG28_ADDRESS 0x34
#define TG28_REG_ENABLE 0x90
#define TG28_ALDO1_VOLTAGE 0x92
#define TG28_ALDO2_VOLTAGE 0x93
#define TG28_ALDO3_ENABLE 0x04
#define TG28_VOLTAGE_MASK 0x1f
#define TG28_VOLTAGE_3300 28 /* (3300 - 500) / 100 mV */
#define TG28_TIMEOUT_MS 100

static const char *TAG = "candis_power";

static esp_err_t read_reg(i2c_master_dev_handle_t pmu, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(pmu, &reg, 1, value, 1, TG28_TIMEOUT_MS);
}

static esp_err_t update_reg(i2c_master_dev_handle_t pmu, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    ESP_RETURN_ON_ERROR(read_reg(pmu, reg, &old), TAG, "read TG28 register 0x%02x", reg);
    const uint8_t bytes[] = {reg, (old & ~mask) | value};
    return old == bytes[1] ? ESP_OK : i2c_master_transmit(pmu, bytes, sizeof(bytes), TG28_TIMEOUT_MS);
}

static esp_err_t open_pmu(candis_power_t *lease)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TG28_ADDRESS, .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(candis_i2c_acquire_locked(&lease->bus), TAG, "acquire main I2C");
    return i2c_master_bus_add_device(lease->bus, &config, &lease->pmu);
}

esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms)
{
    /* The caller holds the board control lock for all PMU register updates. */
    ESP_RETURN_ON_ERROR(open_pmu(lease), TAG, "open TG28 device");
    uint8_t enabled;
    ESP_RETURN_ON_ERROR(read_reg(lease->pmu, TG28_REG_ENABLE, &enabled), TAG, "read rail state");
    const uint8_t voltage_reg = lease->rail == CANDIS_RAIL_DISPLAY ? TG28_ALDO1_VOLTAGE :
                                lease->rail == CANDIS_RAIL_AUDIO ? TG28_ALDO2_VOLTAGE : 0;
    uint8_t voltage = TG28_VOLTAGE_3300;
    if (voltage_reg) {
        ESP_RETURN_ON_ERROR(read_reg(lease->pmu, voltage_reg, &voltage), TAG, "read rail voltage");
    }
    if (enabled & lease->rail) {
        return (voltage & TG28_VOLTAGE_MASK) == TG28_VOLTAGE_3300 ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_FALSE(manage, ESP_ERR_INVALID_STATE, TAG, "requested rail is off");
    ESP_RETURN_ON_FALSE(lease->rail != CANDIS_RAIL_EXPANSION || !(enabled & TG28_ALDO3_ENABLE),
                        ESP_ERR_INVALID_STATE, TAG, "DC1SW is off while ALDO3 is on");
    if (voltage_reg && (voltage & TG28_VOLTAGE_MASK) != TG28_VOLTAGE_3300) {
        lease->voltage_reg = voltage_reg;
        lease->original_voltage = voltage & TG28_VOLTAGE_MASK;
        ESP_RETURN_ON_ERROR(update_reg(lease->pmu, voltage_reg, TG28_VOLTAGE_MASK, TG28_VOLTAGE_3300),
                            TAG, "set rail voltage");
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
    }
    /* Retain ownership even if a bus error occurs after sending the enable write. */
    lease->owned = true;
    ESP_RETURN_ON_ERROR(update_reg(lease->pmu, TG28_REG_ENABLE, lease->rail, lease->rail), TAG, "enable rail");
    vTaskDelay(pdMS_TO_TICKS(settle_ms) + 1);
    ESP_RETURN_ON_ERROR(read_reg(lease->pmu, TG28_REG_ENABLE, &enabled), TAG, "verify rail state");
    if (voltage_reg) {
        ESP_RETURN_ON_ERROR(read_reg(lease->pmu, voltage_reg, &voltage), TAG, "verify rail voltage");
    }
    return (enabled & lease->rail) && (voltage & TG28_VOLTAGE_MASK) == TG28_VOLTAGE_3300
           ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t candis_power_release(candis_power_t *lease)
{
    if (lease->owned) {
        if (lease->rail == CANDIS_RAIL_EXPANSION) {
            uint8_t enabled;
            ESP_RETURN_ON_ERROR(read_reg(lease->pmu, TG28_REG_ENABLE, &enabled), TAG, "read DC1SW and ALDO3");
            ESP_RETURN_ON_FALSE(!(enabled & TG28_ALDO3_ENABLE), ESP_ERR_INVALID_STATE, TAG, "ALDO3 is still on");
        }
        ESP_RETURN_ON_ERROR(update_reg(lease->pmu, TG28_REG_ENABLE, lease->rail, 0), TAG, "disable rail");
        lease->owned = false;
    }
    if (lease->voltage_reg) {
        ESP_RETURN_ON_ERROR(update_reg(lease->pmu, lease->voltage_reg, TG28_VOLTAGE_MASK, lease->original_voltage),
                            TAG, "restore rail voltage");
        lease->voltage_reg = 0;
    }
    if (lease->pmu) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(lease->pmu), TAG, "remove TG28 device");
        lease->pmu = NULL;
    }
    if (lease->bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release_locked(lease->bus), TAG, "release main I2C");
        lease->bus = NULL;
    }
    return ESP_OK;
}

esp_err_t candis_power_read(candis_power_state_t *state)
{
    static candis_power_t lease;
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t enabled;
    esp_err_t err = candis_power_release(&lease);
    if (err == ESP_OK) {
        err = open_pmu(&lease);
        if (err == ESP_OK) {
            err = read_reg(lease.pmu, TG28_REG_ENABLE, &enabled);
        }
        esp_err_t cleanup = candis_power_release(&lease);
        if (err == ESP_OK) {
            err = cleanup;
        }
    }
    if (err == ESP_OK) {
        *state = (candis_power_state_t){
            .panel = !!(enabled & CANDIS_RAIL_DISPLAY),
            .audio = !!(enabled & CANDIS_RAIL_AUDIO),
            .aldo3 = !!(enabled & TG28_ALDO3_ENABLE),
            .dc1sw = !!(enabled & CANDIS_RAIL_EXPANSION),
        };
    }
    candis_control_leave();
    return err;
}
