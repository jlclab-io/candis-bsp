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

/** Configure the active-low digital inputs. Call all Gamepad APIs from one task.
 * DC1SW and ALDO3 must be stable for the lifetime of this input session.
 * The SW5/IMU shared GPIO38 remains under the expansion power/IMU owner.
 */
esp_err_t candis_gamepad_open(const candis_baseboard_info_t *identity);
esp_err_t candis_gamepad_read(candis_gamepad_inputs_t *inputs);
esp_err_t candis_gamepad_close(void);

#ifdef __cplusplus
}
#endif
