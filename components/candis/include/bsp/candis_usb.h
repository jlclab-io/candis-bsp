/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start native CDC. The application's stdio routing is independent.
 * @return ESP_OK if started, ESP_ERR_INVALID_STATE if the OTG PHY is busy
 *         or a previous teardown is pending, ESP_ERR_NOT_SUPPORTED if disabled.
 * @note Use one OTG stack per port. Stop any separate example CDC console first. */
esp_err_t candis_usb_device_start(void);
/** Detach CDC and release the OTG PHY. Retry if teardown reports an error. */
esp_err_t candis_usb_device_stop(void);
/** Nonblocking CDC I/O, reporting bytes read or queued. */
esp_err_t candis_usb_device_read(void *buffer, size_t capacity, size_t *received);
esp_err_t candis_usb_device_write(const void *buffer, size_t bytes, size_t *queued);

typedef enum bsp_usb_host_power_mode_t {
    BSP_USB_HOST_POWER_MODE_AUTO = 0,
    BSP_USB_HOST_POWER_MODE_USB_DEV = BSP_USB_HOST_POWER_MODE_AUTO,
} bsp_usb_host_power_mode_t;

/** Start the native Host stack with the board's autonomous Type-C supply.
 * Pass BSP_USB_HOST_POWER_MODE_AUTO and false. The USB-C source and current
 * limit are set by hardware. Validate the attached source setup first. */
esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA);
/** Release host clients before stopping. Stop releases the stack and PHY.
 * Host and Device require matching stop calls before switching roles.
 * A failed stop may be retried before attempting the new role. */
esp_err_t bsp_usb_host_stop(void);

#ifdef __cplusplus
}
#endif
