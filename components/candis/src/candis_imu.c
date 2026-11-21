/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware 1.2 with a BMI270 on the expansion assembly. */
#include "bsp/candis_imu.h"
#include "bmi270.h"
#include "candis_internal.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bosch BMI270 INT1_IO_CTRL. The polling driver requires INT1 to stay undriven. */
#define BMI270_INT1_IO_CTRL 0x53
#define BMI270_INT1_OUTPUT_ENABLE (1U << 3)
/* Bosch BMI270 datasheet 5.2, DATA_8 through SENSORTIME_2. */
#define BMI270_CHIP_ID_REG_ADDR 0x00
#define BMI270_STATUS 0x03
#define BMI270_AXES_READY 0xc0
#define BMI270_ACC_DATA 0x0c
#define BMI270_DATA_AND_TIME_SIZE 15
#define BMI270_TEMP_DATA 0x22
#define BMI270_PWR_CONF 0x7c
#define BMI270_ADV_POWER_SAVE (1U << 0)

static const char *TAG = "candis_imu";
static candis_power_t s_power = {.rail = CANDIS_RAIL_EXPANSION};
static bmi270_handle_t *s_bmi;
static i2c_master_dev_handle_t s_pending_remove;
static bool s_ready;
static bool s_measuring;
static bool s_configured;
static bool s_first_sample;
static bmi270_config_t s_measurement;
static float s_accel_scale;
static float s_gyro_scale;

static unsigned setting_index(unsigned value, const uint16_t *settings, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        if (settings[i] == value) {
            return i;
        }
    }
    return count;
}

static esp_err_t measurement_config(const candis_imu_config_t *config, bmi270_config_t *measurement)
{
    static const uint16_t rates[] = {25, 50, 100, 200, 400, 800, 1600};
    static const uint16_t accel_ranges[] = {2, 4, 8, 16};
    static const uint16_t gyro_ranges[] = {2000, 1000, 500, 250, 125};
    const unsigned rate = setting_index(config->sample_rate_hz, rates, 7);
    const unsigned accel = setting_index(config->accel_range_g, accel_ranges, 4);
    const unsigned gyro = setting_index(config->gyro_range_dps, gyro_ranges, 5);
    if (rate == 7 || accel == 4 || gyro == 5) {
        return ESP_ERR_INVALID_ARG;
    }
    *measurement = (bmi270_config_t){
        .acce_odr = BMI270_ACC_ODR_25_HZ + rate,
        .acce_range = BMI270_ACC_RANGE_2_G + accel,
        .gyro_odr = BMI270_GYR_ODR_25_HZ + rate,
        .gyro_range = BMI270_GYR_RANGE_2000_DPS + gyro,
    };
    return ESP_OK;
}

static esp_err_t start_measurements(void)
{
    s_measuring = true; /* A failed start may have enabled some sensor blocks. */
    const esp_err_t err = bmi270_start(s_bmi, &s_measurement);
    s_first_sample = true;
    s_ready = err == ESP_OK;
    return err;
}

static esp_err_t set_advanced_power_save(bool enabled)
{
    uint8_t data[2] = {BMI270_PWR_CONF};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_bmi->i2c_handle, data, 1, &data[1], 1, 100),
                        TAG, "read BMI270 power configuration");
    const uint8_t target = enabled ? data[1] | BMI270_ADV_POWER_SAVE : data[1] & ~BMI270_ADV_POWER_SAVE;
    if (target != data[1]) {
        data[1] = target;
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_bmi->i2c_handle, data, sizeof(data), 100),
                            TAG, "set BMI270 advanced power save");
        /* Match the power configuration settling delay used by espressif/bmi270. */
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
    }
    return ESP_OK;
}

static esp_err_t enable_measurements(void)
{
    const bmi270_driver_config_t driver = {
        .addr = CANDIS_IMU_ADDR,
        .interface = BMI270_USE_I2C,
        .i2c_bus = s_power.bus,
    };
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_power.bus, CANDIS_IMU_ADDR, 100), TAG, "probe BMI270");
    ESP_RETURN_ON_ERROR(bmi270_create(&driver, &s_bmi), TAG, "initialize BMI270");
    const uint8_t reg = BMI270_INT1_IO_CTRL;
    uint8_t config;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_bmi->i2c_handle, &reg, 1, &config, 1, 100),
                        TAG, "read BMI270 INT1 configuration");
    ESP_RETURN_ON_FALSE(!(config & BMI270_INT1_OUTPUT_ENABLE), ESP_ERR_INVALID_STATE,
                        TAG, "BMI270 INT1 output must be disabled");
    return start_measurements();
}

static esp_err_t stop_locked(void)
{
    s_ready = false;
    s_configured = false;
    if (s_measuring) {
        ESP_RETURN_ON_ERROR(bmi270_stop(s_bmi), TAG, "stop BMI270");
        s_measuring = false;
    }
    if (s_bmi) {
        const i2c_master_dev_handle_t device = s_bmi->i2c_handle;
        const esp_err_t result = bmi270_delete(s_bmi);
        /* Upstream frees the handle even when I2C device removal fails. */
        s_bmi = NULL;
        if (result != ESP_OK) {
            s_pending_remove = device;
        }
        ESP_RETURN_ON_ERROR(result, TAG, "delete BMI270");
    }
    if (s_pending_remove) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_pending_remove), TAG, "retry BMI270 I2C device removal");
        s_pending_remove = NULL;
    }
    return candis_power_release(&s_power);
}

esp_err_t candis_imu_start(bool manage_power)
{
    candis_imu_config_t config = CANDIS_IMU_CONFIG_DEFAULT();
    config.manage_power = manage_power;
    return candis_imu_start_with_config(&config);
}

esp_err_t candis_imu_start_with_config(const candis_imu_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    bmi270_config_t measurement;
    ESP_RETURN_ON_ERROR(measurement_config(config, &measurement), TAG, "invalid sample configuration");
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_power.bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_measurement = measurement;
    s_accel_scale = config->accel_range_g / 32768.0f;
    s_gyro_scale = config->gyro_range_dps / 32768.0f;
    esp_err_t result = candis_power_acquire(&s_power, config->manage_power, 20);
    if (result == ESP_OK) {
        result = enable_measurements();
    }
    if (result == ESP_OK) {
        s_configured = true;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_locked());
    }
    candis_control_leave();
    return result;
}

esp_err_t candis_imu_suspend(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_configured) {
        s_ready = false;
        err = s_measuring ? bmi270_stop(s_bmi) : ESP_OK;
        if (err == ESP_OK) {
            s_measuring = false;
            err = set_advanced_power_save(true);
        }
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_imu_resume(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (s_configured && !s_ready) {
        err = set_advanced_power_save(false);
        if (err == ESP_OK) {
            err = start_measurements();
        }
    }
    candis_control_leave();
    return err;
}

static esp_err_t probe_chip(candis_capability_state_t *state)
{
    esp_err_t err = i2c_master_probe(s_power.bus, CANDIS_IMU_ADDR, 100);
    if (err == ESP_ERR_NOT_FOUND) {
        *state = CANDIS_CAPABILITY_NOT_PRESENT;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "probe BMI270");
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CANDIS_IMU_ADDR, .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_power.bus, &device, &s_pending_remove), TAG, "add BMI270 probe");
    const uint8_t reg = BMI270_CHIP_ID_REG_ADDR;
    uint8_t chip_id;
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_pending_remove, &reg, 1, &chip_id, 1, 100),
                        TAG, "read BMI270 ID");
    *state = chip_id == BMI270_CHIP_ID ? CANDIS_CAPABILITY_PRESENT : CANDIS_CAPABILITY_UNSUPPORTED;
    return ESP_OK;
}

esp_err_t candis_imu_get_state(candis_capability_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_configured) {
        *state = CANDIS_CAPABILITY_INITIALIZED;
        err = ESP_OK;
    } else if (!s_power.bus) {
        candis_capability_state_t detected;
        err = candis_power_acquire(&s_power, false, 0);
        if (err == ESP_OK) {
            err = probe_chip(&detected);
        }
        const esp_err_t cleanup = stop_locked();
        err = err == ESP_OK ? cleanup : err;
        if (err == ESP_OK) {
            *state = detected;
        }
    }
    candis_control_leave();
    return err;
}

static int32_t decode_axis(const uint8_t *data)
{
    const uint16_t raw = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return raw >= 0x8000 ? (int32_t)raw - 0x10000 : raw;
}

static esp_err_t read_measurement(candis_imu_sample_t *sample)
{
    if (s_first_sample) {
        const uint8_t status_reg = BMI270_STATUS;
        uint8_t status;
        ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_bmi->i2c_handle, &status_reg, 1, &status, 1, 100),
                            TAG, "read BMI270 data-ready status");
        if ((status & BMI270_AXES_READY) != BMI270_AXES_READY) {
            return ESP_ERR_NOT_FINISHED;
        }
        s_first_sample = false;
    }
    const uint8_t start = BMI270_ACC_DATA;
    uint8_t data[BMI270_DATA_AND_TIME_SIZE];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_bmi->i2c_handle, &start, sizeof(start), data, sizeof(data), 100),
                        TAG, "read BMI270 axes and sensor time");
    candis_imu_sample_t value = {
        .timestamp_us = esp_timer_get_time(),
        .sensor_ticks = (uint32_t)data[12] | ((uint32_t)data[13] << 8) | ((uint32_t)data[14] << 16),
    };
    for (unsigned axis = 0; axis < 3; ++axis) {
        value.acceleration_g[axis] = decode_axis(data + axis * 2) * s_accel_scale;
        value.angular_velocity_dps[axis] = decode_axis(data + 6 + axis * 2) * s_gyro_scale;
    }
    *sample = value;
    return ESP_OK;
}

esp_err_t candis_imu_read(candis_imu_sample_t *sample)
{
    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = s_ready ? read_measurement(sample) : ESP_ERR_INVALID_STATE;
    candis_control_leave();
    return result;
}

esp_err_t candis_imu_read_temperature(float *celsius)
{
    if (!celsius) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_ready) {
        const uint8_t reg = BMI270_TEMP_DATA;
        uint8_t data[2];
        result = i2c_master_transmit_receive(s_bmi->i2c_handle, &reg, sizeof(reg), data, sizeof(data), 100);
        if (result == ESP_OK) {
            const int32_t raw = decode_axis(data);
            if (raw == -32768) {
                result = ESP_ERR_NOT_FINISHED;
            } else {
                *celsius = 23.0f + raw / 512.0f;
            }
        }
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
