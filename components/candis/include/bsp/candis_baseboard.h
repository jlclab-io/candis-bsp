/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANDIS_BASEBOARD_IMAGE_SIZE 134

typedef enum {
    CANDIS_BASEBOARD_GAMEPAD = 1,
    CANDIS_BASEBOARD_BB,
} candis_baseboard_kind_t;

typedef struct {
    candis_baseboard_kind_t kind;
    uint16_t hardware_revision;
    uint16_t format_revision;
    char name[33];
} candis_baseboard_info_t;

/** Validate the three CRCs and exact Candis identity tuple. Output is set only on success. */
esp_err_t candis_baseboard_decode(const uint8_t *image, size_t length, candis_baseboard_info_t *info);

/** Read U10 at 0x50 on the expansion I2C bus while DC1SW is already on.
 * GPIO58/59 must be released by UART0 and have no external loopback or adapter.
 * A missing response describes this probe attempt only. No device is activated.
 */
esp_err_t candis_baseboard_probe(candis_baseboard_info_t *info);

#ifdef __cplusplus
}
#endif
