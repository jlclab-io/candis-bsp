/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_MOTOR_PWM GPIO_NUM_5
#define CANDIS_MOTOR_MAX_PULSE_MS 600

typedef struct {
    bool active;
    bool cleanup_pending;
    esp_err_t last_error;
} candis_motor_state_t;

/** Run a 20 kHz PWM pulse on the fitted M1 motor (1–100%, 1–600 ms).
 * Requires valid USB input. This call may enable BLDO2 at 2700 mV.
 * The BSP reserves low-speed LEDC timer and channel 0 for the pulse.
 * Pulse timing uses the FreeRTOS scheduler. */
esp_err_t candis_motor_pulse(unsigned percent, unsigned duration_ms);
/** Cancel output and release resources. Call again if cleanup fails. */
esp_err_t candis_motor_stop(void);
esp_err_t candis_motor_get_state(candis_motor_state_t *state);

#ifdef __cplusplus
}
#endif
