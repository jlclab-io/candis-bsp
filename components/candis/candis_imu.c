/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware 1.2 with a BMI270 on the expansion assembly. */
#include "bsp/candis_imu.h"
#include "bmi270.h"
#include "candis_internal.h"
#include "esp_check.h"
#include "esp_timer.h"

/* Bosch BMI270 INT1_IO_CTRL. Gamepad shares this output with SW5. */
#define BMI270_INT1_IO_CTRL 0x53
#define BMI270_INT1_OUTPUT_ENABLE (1U << 3)

static const char *TAG = "candis_imu";
static candis_power_t s_power = {.rail = CANDIS_RAIL_EXPANSION};
static bmi270_handle_t *s_bmi;
static bool s_ready;
static bool s_measuring;

static esp_err_t enable_measurements(void)
{
    const bmi270_driver_config_t driver = {
        .addr = CANDIS_IMU_ADDR,
        .interface = BMI270_USE_I2C,
        .i2c_bus = s_power.bus,
    };
    ESP_RETURN_ON_ERROR(bmi270_create(&driver, &s_bmi), TAG, "initialize BMI270");
    const uint8_t reg = BMI270_INT1_IO_CTRL;
    uint8_t config;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_bmi->i2c_handle, &reg, 1, &config, 1, 100),
                        TAG, "read BMI270 INT1 configuration");
    ESP_RETURN_ON_FALSE(!(config & BMI270_INT1_OUTPUT_ENABLE), ESP_ERR_INVALID_STATE,
                        TAG, "BMI270 interrupt output active on shared GPIO38");
    const bmi270_config_t measurement = {
        .acce_odr = BMI270_ACC_ODR_100_HZ,
        .acce_range = BMI270_ACC_RANGE_4_G,
        .gyro_odr = BMI270_GYR_ODR_100_HZ,
        .gyro_range = BMI270_GYR_RANGE_1000_DPS,
    };
    s_measuring = true;
    return bmi270_start(s_bmi, &measurement);
}

static esp_err_t stop_locked(void)
{
    s_ready = false;
    if (s_measuring) {
        ESP_RETURN_ON_ERROR(bmi270_stop(s_bmi), TAG, "stop BMI270");
        s_measuring = false;
    }
    if (s_bmi) {
        const esp_err_t result = bmi270_delete(s_bmi);
        /* Upstream frees the handle even when I2C device removal fails. */
        s_bmi = NULL;
        ESP_RETURN_ON_ERROR(result, TAG, "delete BMI270");
    }
    return candis_power_release(&s_power);
}

esp_err_t candis_imu_start(bool manage_power)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_power.bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = candis_power_acquire(&s_power, manage_power, 20);
    if (result == ESP_OK) {
        result = enable_measurements();
    }
    if (result == ESP_OK) {
        s_ready = true;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_locked());
    }
    candis_control_leave();
    return result;
}

esp_err_t candis_imu_read(candis_imu_sample_t *sample)
{
    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_imu_sample_t value;
    float *g = value.acceleration_g;
    float *dps = value.angular_velocity_dps;
    esp_err_t result = s_ready ? bmi270_get_acce_data(s_bmi, g, g + 1, g + 2) : ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        result = bmi270_get_gyro_data(s_bmi, dps, dps + 1, dps + 2);
    }
    if (result == ESP_OK) {
        value.timestamp_us = esp_timer_get_time();
        *sample = value;
    }
    candis_control_leave();
    return result;
}

esp_err_t candis_imu_stop(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = stop_locked();
    candis_control_leave();
    return result;
}
