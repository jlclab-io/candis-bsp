/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/candis_types.h"
#include "bsp/config.h"
#include "driver/i2c_master.h"
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
    i2c_master_bus_handle_t i2c_bus; /**< Caller-owned, already powered bus. */
    uint8_t i2c_address;             /**< Caller-selected 7-bit BMI270 address. */
    uint16_t sample_rate_hz; /**< Both sensors, 25/50/100/200/400/800/1600 Hz. */
    uint8_t accel_range_g;   /**< Full-scale magnitude, 2/4/8/16 g. */
    uint16_t gyro_range_dps; /**< Full-scale magnitude, 125/250/500/1000/2000 dps. */
} candis_imu_config_t;

#define CANDIS_IMU_CONFIG_DEFAULT() { \
    .sample_rate_hz = 100, \
    .accel_range_g = 4, \
    .gyro_range_dps = 1000, \
}

/**
 * @brief Start using a caller-owned I2C bus and caller-selected address.
 * The caller owns sensing power and must keep the bus alive until stop.
 * @return ESP_ERR_INVALID_ARG for unsupported settings, ESP_ERR_NOT_FOUND if
 *         no BMI270 responds, or ESP_ERR_INVALID_STATE if already acquired.
 *         Underlying bus errors propagate.
 */
esp_err_t candis_imu_start_with_config(const candis_imu_config_t *config);

/** Probe the caller-selected bus/address without starting measurements. */
esp_err_t candis_imu_probe(const candis_imu_config_t *config,
                           candis_capability_state_t *state);

/** Stop the sensor blocks and enable advanced power save while retaining the caller's bus. */
esp_err_t candis_imu_suspend(void);
/** Exit advanced power save and resume with the same sampling settings. */
esp_err_t candis_imu_resume(void);

/** Query the active session. Use candis_imu_probe() before starting. */
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

/** Stop measurements and remove the BMI270 device from the caller's bus. */
esp_err_t candis_imu_stop(void);

#ifdef __cplusplus
}
#endif
