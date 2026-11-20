/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "bsp/candis_baseboard.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool joystick_pressed;
    bool encoder_pressed;
    bool encoder_a;
    bool encoder_b;
    bool sw3_pressed;
    bool sw4_pressed;
    bool sw6_pressed;
} candis_gamepad_inputs_t;

typedef enum {
    CANDIS_GAMEPAD_JOYSTICK = 1U << 0,
    CANDIS_GAMEPAD_ENCODER  = 1U << 1,
    CANDIS_GAMEPAD_SW3      = 1U << 2,
    CANDIS_GAMEPAD_SW4      = 1U << 3,
    CANDIS_GAMEPAD_SW5      = 1U << 4,
    CANDIS_GAMEPAD_SW6      = 1U << 5,
} candis_gamepad_button_t;

typedef struct {
    bool axes; /**< Reserve the joystick ADC unit for this session. */
    bool shared_sw5; /**< The caller must keep BMI270 INT1 high impedance for the session. */
    uint16_t debounce_ms;
    bool calibrate_axes; /**< Require the ESP32-S31 ADC calibration scheme when axes are enabled. */
} candis_gamepad_config_t;

#define CANDIS_GAMEPAD_CONFIG_DEFAULT() { .debounce_ms = 10 }

typedef struct {
    uint32_t buttons;
    uint32_t pressed;
    uint32_t released;
    int8_t encoder_steps; /**< Relative movement in complete A/B cycles. */
    bool axes_valid;
    int raw_x; /**< ESP32-S31 comparator vector. Use x_mv for axis mapping. */
    int raw_y;
    bool voltage_valid; /**< Both calibrated voltages were obtained for this sample. */
    int x_mv;
    int y_mv;
} candis_gamepad_sample_t;

/** Configure the active-low digital inputs. Call all Gamepad APIs from one task.
 * DC1SW and ALDO3 must be stable for the lifetime of this input session.
 * The SW5/IMU shared GPIO38 remains under the expansion power/IMU owner.
 */
esp_err_t candis_gamepad_open(const candis_baseboard_info_t *identity);
esp_err_t candis_gamepad_open_with_config(const candis_baseboard_info_t *identity,
                                         const candis_gamepad_config_t *config);
esp_err_t candis_gamepad_read(candis_gamepad_inputs_t *inputs);
/** Poll at a fixed cadence that captures all encoder phases. Initial levels emit no button edges. */
esp_err_t candis_gamepad_poll(candis_gamepad_sample_t *sample);
esp_err_t candis_gamepad_close(void);

#ifdef __cplusplus
}
#endif
