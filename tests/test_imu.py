# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


STUB = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { i2c_master_dev_handle_t i2c_handle; } bmi270_handle_t;
typedef struct { unsigned addr, interface; void *i2c_bus; } bmi270_driver_config_t;
typedef struct { unsigned acce_odr, acce_range, gyro_odr, gyro_range; } bmi270_config_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
enum {
    BMI270_USE_I2C = 0,
    I2C_ADDR_BIT_LEN_7 = 7, BMI270_CHIP_ID = 0x24,
    BMI270_ACC_ODR_25_HZ = 6, BMI270_GYR_ODR_25_HZ = 6,
    BMI270_ACC_RANGE_2_G = 0, BMI270_GYR_RANGE_2000_DPS = 0,
    BMI270_ACC_ODR_100_HZ = 8, BMI270_ACC_RANGE_4_G = 1,
    BMI270_GYR_ODR_100_HZ = 8, BMI270_GYR_RANGE_1000_DPS = 1,
};
bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t bmi270_create(const bmi270_driver_config_t *cfg, bmi270_handle_t **dev);
esp_err_t bmi270_start(bmi270_handle_t *dev, const bmi270_config_t *cfg);
esp_err_t bmi270_stop(const bmi270_handle_t *dev);
esp_err_t bmi270_delete(bmi270_handle_t *dev);
esp_err_t i2c_master_probe(void *bus, uint8_t address, int timeout);
esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int timeout);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *reg, size_t reg_len,
                                    uint8_t *data, size_t count, int timeout);
int64_t esp_timer_get_time(void);
void vTaskDelay(unsigned ticks);
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); \
    esp_err_t error = (call); if (error != ESP_OK) return error; } while (0)
#define ESP_RETURN_ON_FALSE(condition, error, tag, ...) do { (void)(tag); \
    if (!(condition)) return (error); } while (0)
#undef ESP_ERROR_CHECK_WITHOUT_ABORT
#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))
"""

SOURCE = r"""
#include <assert.h>
#include <string.h>
#include "bsp/candis_imu.h"
#include "stub.h"

static int device;
static bool locked, measuring, attached, removal_failure, read_failure;
static bool present = true, data_ready = true;
static uint8_t chip_id = BMI270_CHIP_ID, power_config;
static unsigned accel_setting = BMI270_ACC_RANGE_4_G, gyro_setting = BMI270_GYR_RANGE_1000_DPS;
static unsigned reads, stops;
static uint16_t temperature = 0x8000;
static bmi270_handle_t handle = {.i2c_handle = &device};

bool candis_control_enter(void)
{
    assert(!locked);
    locked = true;
    return true;
}

void candis_control_leave(void)
{
    assert(locked);
    locked = false;
}

esp_err_t bmi270_create(const bmi270_driver_config_t *cfg, bmi270_handle_t **dev)
{
    assert(locked && !attached && cfg->addr == 0x68 && cfg->i2c_bus == &device);
    attached = true;
    *dev = &handle;
    return ESP_OK;
}

esp_err_t bmi270_start(bmi270_handle_t *dev, const bmi270_config_t *cfg)
{
    assert(locked && attached && dev == &handle && cfg->acce_odr == cfg->gyro_odr);
    assert(cfg->acce_range == accel_setting && cfg->gyro_range == gyro_setting && !(power_config & 1));
    measuring = true;
    return ESP_OK;
}

esp_err_t bmi270_stop(const bmi270_handle_t *dev)
{
    assert(locked && measuring && dev == &handle);
    measuring = false;
    ++stops;
    return ESP_OK;
}

esp_err_t bmi270_delete(bmi270_handle_t *dev)
{
    assert(locked && dev == &handle);
    return i2c_master_bus_rm_device(dev->i2c_handle);
}

esp_err_t i2c_master_probe(void *bus, uint8_t addr, int timeout)
{
    assert(locked && bus == &device && addr == 0x68 && timeout == 100);
    return present ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_bus_add_device(void *bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *dev)
{
    assert(locked && !attached && bus == &device && cfg->device_address == 0x68);
    attached = true;
    *dev = &device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert(locked && !measuring && attached && dev == &device);
    if (removal_failure) {
        return ESP_ERR_INVALID_STATE;
    }
    attached = false;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int timeout)
{
    assert(locked && dev == &device && attached && !measuring && len == 2 && data[0] == 0x7c && timeout == 100);
    power_config = data[1];
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *reg, size_t reg_len,
                                    uint8_t *data, size_t count, int timeout)
{
    assert(locked && dev == &device && attached && reg_len == 1 && timeout == 100);
    if (*reg == 0x00 || *reg == 0x7c || *reg == 0x03) {
        assert(count == 1);
        data[0] = *reg == 0x00 ? chip_id : *reg == 0x7c ? power_config : data_ready ? 0xc0 : 0;
        return ESP_OK;
    }
    if (*reg == 0x53) {
        assert(count == 1 && !measuring);
        data[0] = 0;
        return ESP_OK;
    }
    if (*reg == 0x22) {
        assert(measuring && count == 2);
        data[0] = temperature & 0xff;
        data[1] = temperature >> 8;
        return ESP_OK;
    }
    assert(measuring && *reg == 0x0c && count == 15);
    ++reads;
    if (read_failure) {
        return ESP_ERR_TIMEOUT;
    }
    const uint8_t values[] = {0, 0x80, 0, 0, 0, 0x20, 0, 0x40, 0, 0xc0, 0xff, 0x7f, 0x12, 0x34, 0x56};
    memcpy(data, values, sizeof(values));
    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    return INT64_C(10000) * reads;
}

void vTaskDelay(unsigned ticks)
{
    assert(locked && ticks == 11);
}

int main(void)
{
    candis_imu_sample_t sample = {.sensor_ticks = 17};
    candis_imu_config_t config = CANDIS_IMU_CONFIG_DEFAULT();
    config.i2c_bus = &device;
    config.i2c_address = 0x68;
    assert(candis_imu_read(&sample) == ESP_ERR_INVALID_STATE && sample.sensor_ticks == 17);
    assert(candis_imu_start_with_config(&config) == ESP_OK);
    assert(candis_imu_read(&sample) == ESP_OK && reads == 1);
    assert(sample.acceleration_g[0] == -4 && sample.acceleration_g[1] == 0 && sample.acceleration_g[2] == 1);
    assert(sample.angular_velocity_dps[0] == 500 && sample.angular_velocity_dps[1] == -500 &&
           sample.angular_velocity_dps[2] > 999 && sample.angular_velocity_dps[2] < 1000);
    assert(sample.sensor_ticks == 0x563412 && sample.timestamp_us == 10000);
    float celsius = 1;
    assert(candis_imu_read_temperature(NULL) == ESP_ERR_INVALID_ARG);
    assert(candis_imu_read_temperature(&celsius) == ESP_ERR_NOT_FINISHED && celsius == 1);
    temperature = 0xfc00;
    assert(candis_imu_read_temperature(&celsius) == ESP_OK && celsius == 21);
    read_failure = true;
    assert(candis_imu_read(&sample) == ESP_ERR_TIMEOUT && reads == 2);
    assert(sample.sensor_ticks == 0x563412 && sample.acceleration_g[0] == -4);

    removal_failure = true;
    assert(candis_imu_stop() == ESP_ERR_INVALID_STATE && attached && stops == 1);
    assert(candis_imu_read(&sample) == ESP_ERR_INVALID_STATE && reads == 2);
    assert(candis_imu_read_temperature(&celsius) == ESP_ERR_INVALID_STATE && celsius == 21);
    assert(candis_imu_start_with_config(&config) == ESP_ERR_INVALID_STATE);
    removal_failure = false;
    assert(candis_imu_stop() == ESP_OK && !attached && stops == 1);

    candis_capability_state_t state;
    present = false;
    assert(candis_imu_probe(&config, &state) == ESP_OK && state == CANDIS_CAPABILITY_NOT_PRESENT);
    assert(candis_imu_start_with_config(&config) == ESP_ERR_NOT_FOUND && !attached);
    present = true;
    chip_id = 0x13;
    assert(candis_imu_probe(&config, &state) == ESP_OK && state == CANDIS_CAPABILITY_UNSUPPORTED);
    chip_id = BMI270_CHIP_ID;
    assert(candis_imu_probe(&config, &state) == ESP_OK && state == CANDIS_CAPABILITY_PRESENT);

    config.sample_rate_hz = 63;
    assert(candis_imu_start_with_config(&config) == ESP_ERR_INVALID_ARG && !attached);
    config.sample_rate_hz = 200;
    config.accel_range_g = 8;
    config.gyro_range_dps = 2000;
    accel_setting = 2;
    gyro_setting = 0;
    data_ready = false;
    read_failure = false;
    power_config = 0x02;
    assert(candis_imu_start_with_config(&config) == ESP_OK);
    assert(candis_imu_read(&sample) == ESP_ERR_NOT_FINISHED && reads == 2);
    assert(candis_imu_suspend() == ESP_OK && power_config == 3 && attached && !measuring);
    assert(candis_imu_get_state(&state) == ESP_OK && state == CANDIS_CAPABILITY_INITIALIZED);
    assert(candis_imu_read(&sample) == ESP_ERR_INVALID_STATE && reads == 2);
    assert(candis_imu_resume() == ESP_OK && power_config == 2 && measuring);
    data_ready = true;
    assert(candis_imu_read(&sample) == ESP_OK && sample.acceleration_g[0] == -8);
    assert(sample.angular_velocity_dps[0] == 1000);
    assert(candis_imu_stop() == ESP_OK && !attached);
    assert(candis_imu_stop() == ESP_OK && !attached);
}
"""


class ImuTest(unittest.TestCase):
    def test_six_axis_burst_and_device_removal_retry(self):
        component = Path(__file__).resolve().parents[1] / "components/candis"
        idf = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sdkconfig.h").write_text("#define CONFIG_CANDIS_IMU 1\n")
            (root / "stub.h").write_text(STUB)
            for name in ("bmi270.h", "candis_internal.h", "esp_check.h", "esp_timer.h",
                         "driver/i2c_master.h"):
                header = root / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "stub.h"\n')
            (root / "freertos").mkdir()
            for name in ("FreeRTOS.h", "task.h"):
                (root / "freertos" / name).write_text('#include "stub.h"\n')
            driver = root / "candis_imu.c"
            shutil.copyfile(component / "src" / driver.name, driver)
            test = root / "test.c"
            test.write_text(SOURCE)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=undefined", f"-I{root}", f"-I{idf}",
                 f"-I{component / 'include'}", str(driver), str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)
