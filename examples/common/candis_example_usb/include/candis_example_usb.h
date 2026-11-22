/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    CANDIS_EXAMPLE_USB_CONSOLE,
    CANDIS_EXAMPLE_USB_ECHO,
} candis_example_usb_mode_t;

/** Start native OTG CDC. Console redirection requires ESP-IDF 6.2 or later.
 * Echo mode supports ESP-IDF 6.1. Requires the example control task.
 */
esp_err_t candis_example_usb_start(candis_example_usb_mode_t mode, bool allow_download);

/** Detach CDC and release the OTG device before sleeping. */
esp_err_t candis_example_usb_stop(void);

/** Nonblocking console input. Prefer native CDC and otherwise read the boot console. */
bool candis_example_usb_read(char *byte);

/** Guard a long-running operation. The control task can poll for ROM handover. */
bool candis_example_usb_begin_interruptible_operation(void);
bool candis_example_usb_download_pending(void);

/** Block ROM handover while a control task changes hardware state. */
bool candis_example_usb_begin_operation(void);

/** Allow a new download sequence after the hardware state has been restored. */
void candis_example_usb_end_operation(void);
