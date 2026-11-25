/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <esp_err.h>

/** Start the Candis native CDC device and wait briefly for the capture host. */
esp_err_t api_test_console_start(unsigned timeout_ms);
/** Re-open the native CDC device after tests temporarily release or switch USB. */
esp_err_t api_test_console_restore(unsigned timeout_ms);
/** Release the active capture console before exercising the public USB device API. */
esp_err_t api_test_console_stop(void);
/** Print to the ESP console and the native CDC data channel. */
void api_test_emit(const char *format, ...) __attribute__((format(printf, 1, 2)));
