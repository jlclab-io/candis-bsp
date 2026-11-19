/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include "bsp/candis_imu.h"
#include "bsp/candis_power.h"
#include "bsp/candis_storage.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "candis_motion";

static esp_err_t read_samples(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    int64_t previous = 0;
    for (unsigned index = 0; index < 8; ++index) {
        candis_imu_sample_t sample;
        ESP_RETURN_ON_ERROR(candis_imu_read(&sample), TAG, "read six-axis sample");
        ESP_RETURN_ON_FALSE(sample.timestamp_us > previous, ESP_ERR_INVALID_RESPONSE, TAG, "sample timestamp");
        previous = sample.timestamp_us;
        const float *a = sample.acceleration_g;
        const float *g = sample.angular_velocity_dps;
        for (unsigned axis = 0; axis < 3; ++axis) {
            ESP_RETURN_ON_FALSE(isfinite(a[axis]) && fabsf(a[axis]) <= 4.01f &&
                                isfinite(g[axis]) && fabsf(g[axis]) <= 1001.f,
                                ESP_ERR_INVALID_RESPONSE, TAG, "sample range");
        }
        ESP_RETURN_ON_FALSE(a[0] || a[1] || a[2], ESP_ERR_INVALID_RESPONSE, TAG, "zero acceleration");
        ESP_LOGI(TAG, "acc=(%.3f,%.3f,%.3f) g gyro=(%.3f,%.3f,%.3f) dps", a[0], a[1], a[2], g[0], g[1], g[2]);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}

#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
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
    ESP_RETURN_ON_ERROR(candis_storage_mount("/card", CANDIS_STORAGE_FIXTURE, manage, &card), TAG, "mount fixture TF");
    esp_err_t result = candis_imu_start(false);
    esp_err_t unmount = candis_storage_unmount();
    if (result == ESP_OK) {
        result = unmount == ESP_OK ? check_dc1sw(true) : unmount;
    }
    if (result == ESP_OK) {
        result = read_samples();
    }
    esp_err_t stop = candis_imu_stop();
    ESP_RETURN_ON_ERROR(result, TAG, "TF released before IMU");
    ESP_RETURN_ON_ERROR(stop, TAG, "stop IMU");
    return check_dc1sw(initial);
}

static esp_err_t imu_first(bool manage, bool initial)
{
    ESP_RETURN_ON_ERROR(candis_imu_start(manage), TAG, "start fixture IMU");
    sdmmc_card_t *card;
    esp_err_t result = candis_storage_mount("/card", CANDIS_STORAGE_FIXTURE, false, &card);
    esp_err_t stop = candis_imu_stop();
    if (result == ESP_OK) {
        result = stop == ESP_OK ? check_dc1sw(true) : stop;
    }
    if (result == ESP_OK) {
        uint8_t sector[512];
        result = card->csd.sector_size == sizeof(sector) ? sdmmc_read_sectors(card, sector, 0, 1) : ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t unmount = candis_storage_unmount();
    ESP_RETURN_ON_ERROR(result, TAG, "IMU released before TF");
    ESP_RETURN_ON_ERROR(unmount, TAG, "unmount fixture TF");
    return check_dc1sw(initial);
}
#endif

void app_main(void)
{
#ifdef CONFIG_CANDIS_MOTION_MANAGE_POWER
    const bool manage = true;
#else
    const bool manage = false;
#endif
#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
    candis_power_state_t power;
    ESP_ERROR_CHECK(candis_power_read(&power));
    ESP_LOGI(TAG, "Initial DC1SW=%d ALDO3=%d", power.dc1sw, power.aldo3);
#endif
    ESP_ERROR_CHECK(candis_imu_start(manage));
    esp_err_t result = read_samples();
    esp_err_t stop = candis_imu_stop();
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(stop);
    ESP_LOGI(TAG, "IMU standalone start, sample and stop passed");
#ifdef CONFIG_CANDIS_MOTION_TEST_STORAGE
    ESP_ERROR_CHECK(check_dc1sw(power.dc1sw));
    ESP_ERROR_CHECK(storage_first(manage, power.dc1sw));
    ESP_LOGI(TAG, "DC1SW storage-first release passed");
    ESP_ERROR_CHECK(imu_first(manage, power.dc1sw));
    ESP_LOGI(TAG, "DC1SW IMU-first release passed");
#endif
}
