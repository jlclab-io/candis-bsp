/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float acceleration_g[3];
    float angular_velocity_dps[3];
    int64_t timestamp_us;
} candis_imu_sample_t;

/** Start the BMI270 at 100 Hz, ±4 g and ±1000 dps using I2C 0x68.
 * DC1SW powers the expansion devices. Pass true only for a confirmed load.
 * The driver polls both sensors and leaves their interrupt output disabled.
 * Enable CONFIG_CANDIS_IMU to link this API. If cleanup fails, retry stop.
 */
esp_err_t candis_imu_start(bool manage_power);

/** Read acceleration and angular velocity in sensor axes, with an ESP timer timestamp. */
esp_err_t candis_imu_read(candis_imu_sample_t *sample);

/** Stop measurements and release this client’s DC1SW lease. */
esp_err_t candis_imu_stop(void);

#ifdef __cplusplus
}
#endif
