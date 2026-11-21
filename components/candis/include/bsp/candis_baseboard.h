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
    CANDIS_BASEBOARD_BB = 2, /**< Candis Dock. */
} candis_baseboard_kind_t;

typedef struct {
    candis_baseboard_kind_t kind;
    uint16_t hardware_revision;
    uint16_t format_revision;
    char name[33];
} candis_baseboard_info_t;

typedef enum {
    CANDIS_CAPABILITY_UNSUPPORTED,
    CANDIS_CAPABILITY_NOT_PRESENT,
    CANDIS_CAPABILITY_PRESENT,
    CANDIS_CAPABILITY_INITIALIZED,
} candis_capability_state_t;

/** Validate the three CRCs and exact Candis identity tuple. Output is set only on success. */
esp_err_t candis_baseboard_decode(const uint8_t *image, size_t length, candis_baseboard_info_t *info);

/** Probe the powered expansion bus and classify one expected baseboard kind.
 * The caller must release UART0 on GPIO58/59 and keep DC1SW enabled. A
 * successful call returns a state for absence, a valid matching descriptor,
 * or an unsupported identity. Transport and descriptor validation failures
 * return errors without changing the outputs. */
esp_err_t candis_baseboard_probe_state(candis_baseboard_kind_t expected,
                                       candis_capability_state_t *state,
                                       candis_baseboard_info_t *info);

/** Read U10 at 0x50 on the expansion I2C bus while DC1SW is already on.
 * GPIO58/59 must be released by UART0 and have no external loopback or adapter.
 * A missing response describes this probe attempt only. No device is activated.
 */
esp_err_t candis_baseboard_probe(candis_baseboard_info_t *info);

/**
 * @brief Check for an application console or installed UART0 driver before expansion I²C.
 * @return ESP_OK when no software conflict was found, or ESP_ERR_INVALID_STATE.
 * @note The caller must also release external drivers and any custom pin routing.
 */
esp_err_t candis_expansion_i2c_check(void);

/** Probe 1 to 32 selected 7-bit addresses on a powered expansion bus.
 * Release UART0 and any external GPIO58/59 loopback first, as for baseboard_probe().
 * Output bit i indicates an ACK at addresses[i]. Return transport errors separately
 * from a missing ACK. Output is set only after the bus is released successfully.
 */
esp_err_t candis_expansion_probe_addresses(const uint8_t *addresses, size_t count, uint32_t *responding);

#ifdef __cplusplus
}
#endif
