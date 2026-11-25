/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <math.h>
#include <inttypes.h>
#include "bsp/candis.h"
#include "bsp/candis_imu.h"
#include "bsp/candis_power.h"
#include "bsp/candis_storage.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "candis_motion";

static esp_err_t read_samples(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    int64_t previous = 0;
    uint32_t previous_ticks = 0;
    for (unsigned index = 0; index < 8; ++index) {
        candis_imu_sample_t sample;
        const int64_t read_start = esp_timer_get_time();
        ESP_RETURN_ON_ERROR(candis_imu_read(&sample), TAG, "read six-axis sample");
        ESP_RETURN_ON_FALSE(sample.timestamp_us > previous, ESP_ERR_INVALID_RESPONSE, TAG, "sample timestamp");
        previous = sample.timestamp_us;
        const uint32_t elapsed_ticks = (sample.sensor_ticks - previous_ticks) & 0xffffff;
        previous_ticks = sample.sensor_ticks;
        const float *a = sample.acceleration_g;
        const float *g = sample.angular_velocity_dps;
        for (unsigned axis = 0; axis < 3; ++axis) {
            ESP_RETURN_ON_FALSE(isfinite(a[axis]) && fabsf(a[axis]) <= 4.01f &&
                                isfinite(g[axis]) && fabsf(g[axis]) <= 1001.f,
                                ESP_ERR_INVALID_RESPONSE, TAG, "sample range");
        }
        ESP_RETURN_ON_FALSE(a[0] || a[1] || a[2], ESP_ERR_INVALID_RESPONSE, TAG, "zero acceleration");
        ESP_LOGI(TAG, "acc=(%.3f,%.3f,%.3f) g gyro=(%.3f,%.3f,%.3f) dps "
                      "sensor_ticks=%" PRIu32 " delta_ticks=%" PRIu32 " read_us=%" PRId64,
                 a[0], a[1], a[2], g[0], g[1], g[2], sample.sensor_ticks,
                 index ? elapsed_ticks : 0, sample.timestamp_us - read_start);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    float celsius;
    const esp_err_t temperature = candis_imu_read_temperature(&celsius);
    if (temperature == ESP_OK) {
        ESP_LOGI(TAG, "BMI270 die temperature %.2f C", celsius);
    } else if (temperature == ESP_ERR_NOT_FINISHED) {
        ESP_LOGI(TAG, "BMI270 temperature conversion pending");
    } else {
        return temperature;
    }
    return ESP_OK;
}

enum {
    MOTION_IMU_ADDR = 0x68,
};

static i2c_master_bus_handle_t s_imu_bus;

static candis_imu_config_t motion_imu_config(i2c_master_bus_handle_t bus)
{
    candis_imu_config_t config = CANDIS_IMU_CONFIG_DEFAULT();
    config.i2c_bus = bus;
    config.i2c_address = MOTION_IMU_ADDR;
    return config;
}

static esp_err_t imu_start(bool manage_power)
{
    ESP_RETURN_ON_FALSE(!s_imu_bus, ESP_ERR_INVALID_STATE, TAG, "IMU bus already acquired");
    ESP_RETURN_ON_ERROR(candis_expansion_power_acquire(manage_power), TAG, "prepare IMU power");
    esp_err_t err = candis_i2c_acquire(&s_imu_bus);
    if (err == ESP_OK) {
        candis_imu_config_t config = motion_imu_config(s_imu_bus);
        err = candis_imu_start_with_config(&config);
    }
    if (err != ESP_OK) {
        if (s_imu_bus) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(candis_i2c_release(s_imu_bus));
            s_imu_bus = NULL;
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_expansion_power_release());
    }
    return err;
}

static esp_err_t imu_stop(void)
{
    const esp_err_t result = candis_imu_stop();
    if (result == ESP_OK) {
        ESP_RETURN_ON_ERROR(candis_i2c_release(s_imu_bus), TAG, "release IMU I2C");
        s_imu_bus = NULL;
        ESP_RETURN_ON_ERROR(candis_expansion_power_release(), TAG, "release IMU power");
    }
    return result;
}

#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
enum {
    TESTBOARD_SD_CLK = GPIO_NUM_24,
    TESTBOARD_SD_CMD = GPIO_NUM_25,
    TESTBOARD_SD_D0 = GPIO_NUM_20,
    TESTBOARD_SD_D1 = GPIO_NUM_21,
    TESTBOARD_SD_D2 = GPIO_NUM_22,
    TESTBOARD_SD_D3 = GPIO_NUM_23,
    TESTBOARD_SD_CD = GPIO_NUM_35,
};

static candis_storage_config_t testboard_storage_config(void)
{
    candis_storage_config_t config = CANDIS_STORAGE_CONFIG_DEFAULT();
    config.slot.clk = TESTBOARD_SD_CLK;
    config.slot.cmd = TESTBOARD_SD_CMD;
    config.slot.d0 = TESTBOARD_SD_D0;
    config.slot.d1 = TESTBOARD_SD_D1;
    config.slot.d2 = TESTBOARD_SD_D2;
    config.slot.d3 = TESTBOARD_SD_D3;
    config.slot.cd = TESTBOARD_SD_CD;
    config.slot.wp = SDMMC_SLOT_NO_WP;
    config.slot.width = 4;
    config.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    config.card_detect_active_low = true;
    config.use_dc1sw = true;
    return config;
}

static esp_err_t check_dc1sw(bool expected)
{
    candis_power_state_t power;
    ESP_RETURN_ON_ERROR(candis_power_read(&power), TAG, "read DC1SW");
    ESP_LOGI(TAG, "DC1SW expected=%d observed=%d", expected, power.dc1sw);
    return power.dc1sw == expected ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t storage_first(bool manage, bool initial)
{
    sdmmc_card_t *card;
    ESP_RETURN_ON_ERROR(candis_storage_mount("/card", manage, &card), TAG, "mount TF");
    esp_err_t result = imu_start(false);
    esp_err_t unmount = candis_storage_unmount();
    if (result == ESP_OK) {
        result = unmount == ESP_OK ? check_dc1sw(true) : unmount;
    }
    if (result == ESP_OK) {
        result = read_samples();
    }
    esp_err_t stop = imu_stop();
    ESP_RETURN_ON_ERROR(result, TAG, "TF released before IMU");
    ESP_RETURN_ON_ERROR(stop, TAG, "stop IMU");
    return check_dc1sw(initial);
}

static esp_err_t imu_first(bool manage, bool initial)
{
    ESP_RETURN_ON_ERROR(imu_start(manage), TAG, "start IMU");
    sdmmc_card_t *card;
    esp_err_t result = candis_storage_mount("/card", false, &card);
    esp_err_t stop = imu_stop();
    if (result == ESP_OK) {
        result = stop == ESP_OK ? check_dc1sw(true) : stop;
    }
    if (result == ESP_OK) {
        uint8_t sector[512];
        result = card->csd.sector_size == sizeof(sector) ? sdmmc_read_sectors(card, sector, 0, 1) : ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t unmount = candis_storage_unmount();
    ESP_RETURN_ON_ERROR(result, TAG, "IMU released before TF");
    ESP_RETURN_ON_ERROR(unmount, TAG, "unmount TF");
    return check_dc1sw(initial);
}
#endif

static esp_err_t run_motion_test(bool manage)
{
#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
    const candis_storage_config_t storage = testboard_storage_config();
    ESP_RETURN_ON_ERROR(candis_storage_configure(&storage), TAG, "configure TF hardware");
    candis_power_state_t power;
    ESP_RETURN_ON_ERROR(candis_power_read(&power), TAG, "read initial expansion power");
    ESP_LOGI(TAG, "Initial DC1SW=%d ALDO3=%d", power.dc1sw, power.aldo3);
#endif
    ESP_RETURN_ON_ERROR(imu_start(manage), TAG, "start IMU");
    esp_err_t result = read_samples();
    esp_err_t stop = imu_stop();
    ESP_RETURN_ON_ERROR(result, TAG, "sample IMU");
    ESP_RETURN_ON_ERROR(stop, TAG, "stop IMU");
    ESP_LOGI(TAG, "IMU standalone start, sample and stop passed");
#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
    ESP_RETURN_ON_ERROR(check_dc1sw(power.dc1sw), TAG, "restore initial DC1SW");
    ESP_RETURN_ON_ERROR(storage_first(manage, power.dc1sw), TAG, "verify storage-first release");
    ESP_LOGI(TAG, "DC1SW storage-first release passed");
    ESP_RETURN_ON_ERROR(imu_first(manage, power.dc1sw), TAG, "verify IMU-first release");
    ESP_LOGI(TAG, "DC1SW IMU-first release passed");
    candis_storage_reset_config();
#endif
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
#ifdef CONFIG_CANDIS_MOTION_MANAGE_POWER
    const bool manage = true;
#else
    const bool manage = false;
#endif
    const esp_err_t err = run_motion_test(manage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Motion test stopped: %s", esp_err_to_name(err));
    }
}
