/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis_baseboard.h"
#include "bsp/candis_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount the Dock card using GPIO39 and an existing, powered DC1SW domain. */
esp_err_t candis_dock_mount(const candis_baseboard_info_t *identity, const char *path, sdmmc_card_t **card);
/** Mount from a verified Dock descriptor. Opt in to DC1SW control only with the
 * entire expansion load confirmed. Release with candis_storage_unmount().
 */
esp_err_t candis_dock_mount_with_power(const candis_baseboard_info_t *identity, const char *path,
                                      bool manage_power, sdmmc_card_t **card);
/** Mount a Dock card with an explicit VFS and power policy. */
esp_err_t candis_dock_mount_with_config(const candis_baseboard_info_t *identity, const char *path,
                                        const candis_storage_mount_config_t *config, sdmmc_card_t **card);
/** Claim the Dock GPIO39 detect input while the existing DC1SW domain is powered.
 * Read and release with candis_storage_detect_read() and candis_storage_detect_stop().
 */
esp_err_t candis_dock_detect_start(const candis_baseboard_info_t *identity);

typedef struct {
    bool address_51;
    bool address_52;
} candis_dock_module_scan_t;

/** Check ACKs at the two expected EEPROM addresses on a verified Dock.
 * The caller verifies the attached modules and releases UART0 before probing.
 * Output describes address responses. It is set only on success.
 */
esp_err_t candis_dock_probe_modules(const candis_baseboard_info_t *identity, candis_dock_module_scan_t *scan);

#ifdef __cplusplus
}
#endif
