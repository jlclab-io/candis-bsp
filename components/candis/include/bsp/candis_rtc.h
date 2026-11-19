/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read the RX8130CE calendar in UTC (2000–2099). tm_isdst is set to zero.
 * Returns ESP_ERR_INVALID_STATE if the RTC reports a loss of validity or STOP.
 * Does not change RTC flags, backup selection or shared interrupt configuration.
 */
esp_err_t candis_rtc_get_time(struct tm *utc);

/** Set the UTC calendar (2000–2099), deriving the weekday from the date.
 * The RTC timer and interrupt enables must be idle. With recover_invalid=true,
 * VLF authorizes reinitializing idle alarm, timer, offset and backup settings.
 * An enabled backup charger prevents recovery. TG28 and its IRQ are untouched.
 */
esp_err_t candis_rtc_set_time(const struct tm *utc, bool recover_invalid);

#ifdef __cplusplus
}
#endif
