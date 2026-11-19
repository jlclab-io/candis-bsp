/* SPDX-License-Identifier: Apache-2.0 */
/* Candis Dock hardware revision 1.2. */
#include "bsp/candis_dock.h"

esp_err_t candis_dock_mount(const candis_baseboard_info_t *identity, const char *path, sdmmc_card_t **card)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB) {
        return ESP_ERR_INVALID_ARG;
    }
    return candis_storage_mount(path, CANDIS_STORAGE_EXPANSION, false, card);
}
