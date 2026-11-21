/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** SDMMC card detect profiles. Candis exposes the SDMMC bus on its expansion connector. */
typedef enum {
    CANDIS_STORAGE_EXPANSION, /**< Candis Dock, GPIO39 card detect. */
    CANDIS_STORAGE_FIXTURE,   /**< Candis validation fixture, GPIO35 card detect. */
} candis_storage_profile_t;

typedef struct {
    bool manage_power;
    bool read_only; /**< Reject writes to the mounted FAT volume. Requires ESP-IDF 6.2 or later; ignored on earlier versions. */
} candis_storage_mount_config_t;

/** Standard ESP-BSP SDMMC mount configuration.
 * Candis accepts the SDMMC member and validates it against the Dock wiring.
 * SPI mode is intentionally not exposed because SPI2 is owned by the AMOLED. */
typedef struct {
    const esp_vfs_fat_sdmmc_mount_config_t *mount;
    sdmmc_host_t *host;
    union {
        const sdmmc_slot_config_t *sdmmc;
        const sdspi_device_config_t *sdspi;
    } slot;
} bsp_sdcard_cfg_t;

#ifdef CONFIG_BSP_SD_MOUNT_POINT
#define BSP_SD_MOUNT_POINT CONFIG_BSP_SD_MOUNT_POINT
#else
#define BSP_SD_MOUNT_POINT "/sdcard"
#endif

typedef enum {
    BSP_FEATURE_SD = 0,
} bsp_feature_t;

/**
 * @brief Acquire or release the standard SD feature's share of DC1SW.
 * @pre To enable, confirm the Dock load budget and release UART0 on GPIO58/59.
 *      This may power the entire expansion. The Dock identity is then checked.
 * @return ESP_OK for a successful or repeated operation, ESP_ERR_INVALID_ARG
 *         for an unknown feature, or an error from PMU or Dock identification.
 * @note A mount holds its own power reference. Close files and unmount independently.
 */
esp_err_t bsp_feature_enable(bsp_feature_t feature, bool enable);

/**
 * @brief Mount the verified Dock TF card with SDMMC at BSP_SD_MOUNT_POINT.
 * @pre DC1SW is on, UART0 has released GPIO58/59, and U6 leaves GPIO39 high impedance.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if the Dock or card is absent,
 *         ESP_ERR_INVALID_STATE if the bus, host, or application console is busy.
 *         An unreadable descriptor or volume returns its underlying error. Never formats.
 */
esp_err_t bsp_sdcard_mount(void);
/** Close all files first. Releases the mount started by bsp_sdcard_mount(). */
esp_err_t bsp_sdcard_unmount(void);
/** Borrow the current BSP card handle. Valid until the matching unmount. */
sdmmc_card_t *bsp_sdcard_get_handle(void);
/** Fill the fixed Candis SDMMC host configuration. */
void bsp_sdcard_get_sdmmc_host(const int slot, sdmmc_host_t *config);
/** Fill the fixed Candis SDMMC slot configuration. */
void bsp_sdcard_sdmmc_get_slot(const int slot, sdmmc_slot_config_t *config);
/** Fill a generic SDSPI host configuration. Candis does not expose an SDSPI mount path. */
void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config);
/** Fill an SDSPI slot placeholder. Candis does not expose an SDSPI mount path. */
void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config);
/** Mount with the standard ESP-BSP SDMMC configuration structure. */
esp_err_t bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg);
/** SPI mode is not wired on Candis because SPI2 is owned by the AMOLED. */
esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg);

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

/**
 * @brief Check physical presence during a BSP or Candis mount.
 * @return ESP_OK while present, ESP_ERR_NOT_FOUND after removal until unmounted,
 *         ESP_ERR_INVALID_STATE without an active mount or while changing its state.
 * @note Poll from the storage owner. On removal, stop I/O, close files, and call the
 *       matching unmount before reinserting or remounting. Callers own their open files.
 */
esp_err_t candis_storage_check_card(void);

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
