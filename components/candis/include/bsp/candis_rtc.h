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

/** State owned by the caller. Initialize to zero and retain across deep sleep. */
typedef struct {
    bool restore_pending;
    bool minute_updates;
} candis_rtc_update_irq_t;

/** Configure the idle RX8130CE to pulse the shared GPIO0 line at each second update.
 * Requires a valid calendar and exclusive use of the RTC interrupt configuration.
 * The app configures GPIO0 and any ESP sleep wake sources. TG28 is untouched.
 * If restore_pending remains set on error, retry candis_rtc_disarm_update_irq().
 */
esp_err_t candis_rtc_arm_update_irq(candis_rtc_update_irq_t *state);

/** Read the RTC update flag, disable its interrupt, and restore the update interval.
 * Call on wake or if sleep is canceled. updated may be NULL. An RTC update flag
 * alone does not identify the source of a shared GPIO0 interrupt.
 */
esp_err_t candis_rtc_disarm_update_irq(candis_rtc_update_irq_t *state, bool *updated);

#ifdef __cplusplus
}
#endif
