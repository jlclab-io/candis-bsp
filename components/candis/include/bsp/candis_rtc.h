/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

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

#ifdef __cplusplus
}
#endif
