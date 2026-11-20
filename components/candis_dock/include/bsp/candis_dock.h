/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis_baseboard.h"
#include "bsp/candis_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read and validate the Dock descriptor while DC1SW is already powered.
 * The caller must release UART0 on GPIO58/59 and any external driver on those pins.
 * A valid descriptor for another board returns ESP_ERR_NOT_SUPPORTED. On error,
 * the caller's identity is untouched.
 */
esp_err_t candis_dock_probe(candis_baseboard_info_t *identity);

/** Hold the BSP's application DC1SW lease and identify the Dock. Set manage_power
 * only after checking the total attached load (TF, IMU, EEPROM and modules).
 * The GPIO58/59 requirements of candis_dock_probe() apply.
 * While using this lifecycle, reserve candis_expansion_power_acquire()/release()
 * for this component. BSP resources with their own DC1SW leases can coexist.
 */
esp_err_t candis_dock_open(bool manage_power, candis_baseboard_info_t *identity);

/** Call after unmounting the card and stopping detection. Repeated close is safe.
 * Retry close if release failed, including when open logged a rollback failure.
 */
esp_err_t candis_dock_close(void);

/** Interleaved PCM slot connected to the Dock's CN3 speaker connector. */
typedef enum {
    CANDIS_DOCK_SPEAKER_LEFT = 0,
    CANDIS_DOCK_SPEAKER_RIGHT = 1,
} candis_dock_speaker_channel_t;

/** Resolve CN3 from a verified Dock descriptor. Output is set only on success. */
esp_err_t candis_dock_get_speaker_channel(const candis_baseboard_info_t *identity,
                                         candis_dock_speaker_channel_t *channel);

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
 * The module at U6 must leave its shared GPIO39 connection high impedance.
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
