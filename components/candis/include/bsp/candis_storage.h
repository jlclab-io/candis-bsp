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

typedef struct {
    bool manage_power;
    bool read_only; /**< Reject writes to the mounted FAT volume. */
} candis_storage_mount_config_t;

/** Mount a FAT volume in four-bit mode. The card is never formatted.
 * Enabling DC1SW also powers other devices on that domain. Set manage_power only
 * when this mount is the owner of the domain and the connected load is known.
 * Keep the returned card valid by closing all files before unmounting.
 */
esp_err_t candis_storage_mount(const char *base_path, candis_storage_profile_t profile,
                               bool manage_power, sdmmc_card_t **card);
/** Mount with an explicit FAT VFS write policy. Card detect can be active for the same profile. */
esp_err_t candis_storage_mount_with_config(const char *base_path, candis_storage_profile_t profile,
                                           const candis_storage_mount_config_t *config, sdmmc_card_t **card);

/** Release the mount, pins and power lease. Call again if power release fails. */
esp_err_t candis_storage_unmount(void);

/** Hold a reference to an already powered DC1SW domain and claim the active-low card detect input.
 * Reserve the card detect pin for this profile. Any mount during detection must use the same profile.
 * Call detect_stop() after an error to retry any pending release.
 */
esp_err_t candis_storage_detect_start(candis_storage_profile_t profile);

/** Read the raw switch level as card presence. Debounce it in the caller. */
esp_err_t candis_storage_detect_read(bool *present);
/** Release the switch input and DC1SW reference after unmount. Retry on a release error. */
esp_err_t candis_storage_detect_stop(void);

#ifdef __cplusplus
}
#endif
