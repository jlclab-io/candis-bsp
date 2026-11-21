/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/candis_baseboard.h"
#include "bsp/config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float acceleration_g[3];
    float angular_velocity_dps[3];
    int64_t timestamp_us;
    uint32_t sensor_ticks; /**< BMI270 counter at burst read, modulo 2^24, 39.0625 us per tick. */
} candis_imu_sample_t;

typedef struct {
    bool manage_power;
    uint16_t sample_rate_hz; /**< Both sensors, 25/50/100/200/400/800/1600 Hz. */
    uint8_t accel_range_g;   /**< Full-scale magnitude, 2/4/8/16 g. */
    uint16_t gyro_range_dps; /**< Full-scale magnitude, 125/250/500/1000/2000 dps. */
} candis_imu_config_t;

#define CANDIS_IMU_CONFIG_DEFAULT() { .sample_rate_hz = 100, .accel_range_g = 4, .gyro_range_dps = 1000 }

/** Start the BMI270 at 100 Hz, ±4 g and ±1000 dps using I2C 0x68.
 * DC1SW powers the expansion devices. Pass true only for a confirmed load.
 * The driver polls both sensors and leaves their interrupt output disabled.
 * Enable CONFIG_CANDIS_IMU to link this API. If cleanup fails, retry stop.
 */
esp_err_t candis_imu_start(bool manage_power);

/**
 * @brief Start using the supplied configuration and acquire one DC1SW lease.
 * @return ESP_ERR_INVALID_ARG for unsupported settings, ESP_ERR_NOT_FOUND if
 *         no BMI270 responds, or ESP_ERR_INVALID_STATE if already acquired or the
 *         rail is off without permission to enable it. Underlying bus errors propagate.
 */
esp_err_t candis_imu_start_with_config(const candis_imu_config_t *config);

/** Stop the sensor blocks and enable advanced power save. Keeps the DC1SW lease. */
esp_err_t candis_imu_suspend(void);
/** Exit advanced power save and resume with the same sampling settings. */
esp_err_t candis_imu_resume(void);

/** Query the active session or read chip ID at 0x68 while DC1SW is powered. */
#if BSP_CAPS_IMU
esp_err_t candis_imu_get_state(candis_capability_state_t *state);
#else
static inline esp_err_t candis_imu_get_state(candis_capability_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = CANDIS_CAPABILITY_UNSUPPORTED;
    return ESP_OK;
}
#endif

/** Read shadowed acceleration, angular velocity and sensor time in one I2C burst.
 * timestamp_us is the host time at read completion. sensor_ticks is the device
 * counter sampled with the burst. Output is set only on success.
 * ESP_ERR_NOT_FINISHED means the first sample after start or resume is pending.
 */
esp_err_t candis_imu_read(candis_imu_sample_t *sample);

/** Read the BMI270 die temperature in °C. ESP_ERR_NOT_FINISHED means the first conversion is pending. */
esp_err_t candis_imu_read_temperature(float *celsius);

/** Stop measurements and release this client’s DC1SW lease. */
esp_err_t candis_imu_stop(void);

#ifdef __cplusplus
}
#endif
