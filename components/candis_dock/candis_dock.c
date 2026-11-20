/* SPDX-License-Identifier: Apache-2.0 */
/* Candis Dock hardware revision 1.2. */
#include "bsp/candis_dock.h"

esp_err_t candis_dock_mount(const candis_baseboard_info_t *identity, const char *path, sdmmc_card_t **card)
{
    return candis_dock_mount_with_power(identity, path, false, card);
}

esp_err_t candis_dock_mount_with_power(const candis_baseboard_info_t *identity, const char *path,
                                      bool manage_power, sdmmc_card_t **card)
{
    const candis_storage_mount_config_t config = {.manage_power = manage_power};
    return candis_dock_mount_with_config(identity, path, &config, card);
}

esp_err_t candis_dock_mount_with_config(const candis_baseboard_info_t *identity, const char *path,
                                        const candis_storage_mount_config_t *config, sdmmc_card_t **card)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB) {
        return ESP_ERR_INVALID_ARG;
    }
    return candis_storage_mount_with_config(path, CANDIS_STORAGE_EXPANSION, config, card);
}

esp_err_t candis_dock_detect_start(const candis_baseboard_info_t *identity)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB) {
        return ESP_ERR_INVALID_ARG;
    }
    return candis_storage_detect_start(CANDIS_STORAGE_EXPANSION);
}

esp_err_t candis_dock_probe_modules(const candis_baseboard_info_t *identity, candis_dock_module_scan_t *scan)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB || !scan) {
        return ESP_ERR_INVALID_ARG;
    }
    static const uint8_t addresses[] = {0x51, 0x52};
    uint32_t responding;
    const esp_err_t err = candis_expansion_probe_addresses(addresses, sizeof(addresses), &responding);
    if (err == ESP_OK) {
        *scan = (candis_dock_module_scan_t){
            .address_51 = (responding & 1) != 0,
            .address_52 = (responding & 2) != 0,
        };
    }
    return err;
}
