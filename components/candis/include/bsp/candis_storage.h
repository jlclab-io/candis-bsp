/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** SDMMC card detect profiles. Candis exposes the SDMMC bus on its expansion connector. */
typedef enum {
    CANDIS_STORAGE_EXPANSION, /**< BB or Gamepad, GPIO39 card detect. */
    CANDIS_STORAGE_FIXTURE,   /**< Candis validation fixture, GPIO35 card detect. */
} candis_storage_profile_t;

/** Mount a FAT volume in four-bit mode. The card is never formatted.
 * Enabling DC1SW also powers other devices on that domain. Set manage_power only
 * when this mount is the owner of the domain and the connected load is known.
 * Keep the returned card valid by closing all files before unmounting.
 */
esp_err_t candis_storage_mount(const char *base_path, candis_storage_profile_t profile,
                               bool manage_power, sdmmc_card_t **card);

/** Release the mount, pins and power lease. Call again if power release fails. */
esp_err_t candis_storage_unmount(void);

#ifdef __cplusplus
}
#endif
