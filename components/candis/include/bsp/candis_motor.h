/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "bsp/candis_power.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANDIS_MOTOR_MAX_PULSE_MS 600

typedef struct {
    gpio_num_t pwm_gpio;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    ledc_timer_t timer;
    uint32_t frequency_hz;
    ledc_timer_bit_t duty_resolution;
    uint8_t amplitude_min_duty_percent;
    uint8_t amplitude_max_duty_percent;
    bool require_usb;
    bool use_ldo;
    candis_expansion_ldo_t supply_rail;
    uint16_t supply_mv;
    unsigned supply_settle_ms;
} candis_motor_config_t;

#define CANDIS_MOTOR_CONFIG_DEFAULT() { \
    .pwm_gpio = GPIO_NUM_NC, \
    .speed_mode = LEDC_LOW_SPEED_MODE, \
    .channel = LEDC_CHANNEL_0, \
    .timer = LEDC_TIMER_0, \
    .frequency_hz = 20000, \
    .duty_resolution = LEDC_TIMER_10_BIT, \
    .amplitude_min_duty_percent = 20, \
    .amplitude_max_duty_percent = 100, \
    .require_usb = true, \
    .use_ldo = false, \
}

typedef struct {
    bool active;
    bool cleanup_pending;
    uint8_t amplitude_percent;
    uint8_t duty_percent;
    uint32_t frequency_hz;
    esp_err_t last_error;
} candis_motor_state_t;

/** Configure caller-selected PWM and optional PMU supply. No output may be active.
 * amplitude_min_duty_percent sets the first nonzero drive level and
 * amplitude_max_duty_percent sets amplitude 100. Both are 1-100. */
esp_err_t candis_motor_init(const candis_motor_config_t *config);

/** Map a calibrated amplitude (0-100) to a PWM duty. Returns 0 when unconfigured. */
uint8_t candis_motor_amplitude_to_duty(uint8_t amplitude_percent);
/** Map a PWM duty back to the calibrated amplitude where possible. */
uint8_t candis_motor_duty_to_amplitude(uint8_t duty_percent);

/** Start a continuous output at 1-100 percent calibrated vibration amplitude. */
esp_err_t candis_motor_start_amplitude(uint8_t amplitude_percent);
/** Change the amplitude of an active output. Amplitude 0 stops the output. */
esp_err_t candis_motor_set_amplitude(uint8_t amplitude_percent);
/** Change the PWM carrier frequency, including while output is active. */
esp_err_t candis_motor_set_frequency(uint32_t frequency_hz);

/** Start a continuous raw-PWM output at 1-100 percent duty. */
esp_err_t candis_motor_start(unsigned duty_percent);
/** Change raw duty of an active output. Continuous effects use set_amplitude(). */
esp_err_t candis_motor_set_duty(unsigned duty_percent);

/** Run a bounded PWM pulse (1–100%, 1–600 ms) on the configured output.
 * The caller owns GPIO and supply selection. Timing uses the FreeRTOS scheduler. */
esp_err_t candis_motor_pulse(unsigned percent, unsigned duration_ms);
/** Run a calibrated-amplitude pulse for 1-600 ms. */
esp_err_t candis_motor_pulse_amplitude(uint8_t amplitude_percent, unsigned duration_ms);
/** Cancel continuous or timed output and release resources. Call again if cleanup fails. */
esp_err_t candis_motor_stop(void);
esp_err_t candis_motor_get_state(candis_motor_state_t *state);

#ifdef __cplusplus
}
#endif
