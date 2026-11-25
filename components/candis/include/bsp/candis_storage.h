/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Caller-selected SDMMC hardware. The core BSP does not imply a baseboard slot. */
typedef struct {
    sdmmc_slot_config_t slot;
    bool card_detect_active_low;
    bool use_dc1sw;
} candis_storage_config_t;

#define CANDIS_STORAGE_CONFIG_DEFAULT() { \
    .slot = SDMMC_SLOT_CONFIG_DEFAULT(), \
    .card_detect_active_low = true, \
    .use_dc1sw = false, \
}

/** Configure the storage transport. No operation may be active.
 * The slot is copied and may use any available GPIO assignment. Set slot.cd to
 * SDMMC_SLOT_NO_CD when card detection is not wired. Set use_dc1sw only when the
 * selected baseboard derives card power from that core PMU rail. */
esp_err_t candis_storage_configure(const candis_storage_config_t *config);
/** Forget the runtime configuration after all storage and detect operations stop. */
void candis_storage_reset_config(void);

typedef struct {
    bool manage_power;
    bool read_only; /**< Reject FAT writes. Requires ESP-IDF 6.2 or later; earlier versions return ESP_ERR_NOT_SUPPORTED. */
} candis_storage_mount_config_t;

/** Standard ESP-BSP SDMMC mount configuration.
 * Host and slot are caller-owned. Passing them lets applications select pins
 * independently of candis_storage_configure(). Format-if-mount-failed is rejected. */
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

/** Mount the configured SDMMC slot at BSP_SD_MOUNT_POINT. Never formats. */
esp_err_t bsp_sdcard_mount(void);
/** Close all files first. Releases the mount started by bsp_sdcard_mount(). */
esp_err_t bsp_sdcard_unmount(void);
/** Borrow the current BSP card handle. Valid until the matching unmount. */
sdmmc_card_t *bsp_sdcard_get_handle(void);
/** Fill a generic SDMMC host configuration. Pins are supplied by the slot. */
void bsp_sdcard_get_sdmmc_host(const int slot, sdmmc_host_t *config);
/** Copy the slot supplied to candis_storage_configure(). */
void bsp_sdcard_sdmmc_get_slot(const int slot, sdmmc_slot_config_t *config);
/** Fill a generic SDSPI host configuration. */
void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config);
/** Fill an SDSPI slot placeholder for the caller to complete. */
void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config);
/** Mount caller-supplied SDMMC hardware. Never formats. */
esp_err_t bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg);
/** SDSPI mounting is not implemented by this component. */
esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg);

/** Mount a configured FAT volume in one- or four-bit mode. The card is never formatted.
 * manage_power may enable DC1SW only when the configured transport opted into it.
 * Keep the returned card valid by closing all files before unmounting. */
esp_err_t candis_storage_mount(const char *base_path, bool manage_power, sdmmc_card_t **card);
/** Mount with an explicit FAT VFS write policy. */
esp_err_t candis_storage_mount_with_config(const char *base_path,
                                           const candis_storage_mount_config_t *config,
                                           sdmmc_card_t **card);

/** Release the mount, pins and power lease. Call again if power release fails. */
esp_err_t candis_storage_unmount(void);

/** Check physical presence during an active mount. */
esp_err_t candis_storage_check_card(void);

/** Claim the configured card detect input and optional DC1SW reference.
 * Requires candis_storage_configure(). */
esp_err_t candis_storage_detect_start(bool manage_power);
/** Read the raw switch level as card presence. Debounce it in the caller. */
esp_err_t candis_storage_detect_read(bool *present);
/** Release the switch input and optional DC1SW reference. */
esp_err_t candis_storage_detect_stop(void);

#ifdef __cplusplus
}
#endif
