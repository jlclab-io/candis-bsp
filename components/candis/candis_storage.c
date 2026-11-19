/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2, SDMMC routed through CN1. */
#include <stdlib.h>
#include <string.h>
#include "bsp/candis_storage.h"
#include "candis_internal.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "candis_storage";
static struct {
    sdmmc_card_t *card;
    char *path;
    candis_power_t power;
} s_storage;

static esp_err_t mount_card(candis_storage_profile_t profile)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = CANDIS_SD_CLK;
    slot.cmd = CANDIS_SD_CMD;
    slot.d0 = CANDIS_SD_D0;
    slot.d1 = CANDIS_SD_D1;
    slot.d2 = CANDIS_SD_D2;
    slot.d3 = CANDIS_SD_D3;
    slot.cd = profile == CANDIS_STORAGE_FIXTURE ? CANDIS_FIXTURE_SD_CD : CANDIS_EXP_SD_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.width = 4;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_mount_config_t config = {.max_files = 4};
    return esp_vfs_fat_sdmmc_mount(s_storage.path, &host, &slot, &config, &s_storage.card);
}

static esp_err_t release_storage(void)
{
    esp_err_t unmount = ESP_OK;
    if (s_storage.card) {
        /* IDF consumes the card handle before returning its final VFS status. */
        unmount = esp_vfs_fat_sdcard_unmount(s_storage.path, s_storage.card);
        s_storage.card = NULL;
    }
    free(s_storage.path);
    s_storage.path = NULL;
    esp_err_t power = candis_power_release(&s_storage.power);
    if (power != ESP_OK) {
        ESP_LOGE(TAG, "release DC1SW: %s", esp_err_to_name(power));
    }
    return unmount == ESP_OK ? power : unmount;
}

esp_err_t candis_storage_mount(const char *base_path, candis_storage_profile_t profile,
                               bool manage_power, sdmmc_card_t **card)
{
    if (!base_path || base_path[0] != '/' || base_path[1] == '\0' || !card ||
        (profile != CANDIS_STORAGE_EXPANSION && profile != CANDIS_STORAGE_FIXTURE)) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.path || s_storage.power.bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_storage.path = strdup(base_path);
    s_storage.power.rail = CANDIS_RAIL_EXPANSION;
    esp_err_t err = s_storage.path ? candis_power_acquire(&s_storage.power, manage_power, 20) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = mount_card(profile);
    }
    if (err == ESP_OK) {
        *card = s_storage.card;
    } else {
        (void)release_storage();
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = release_storage();
    candis_control_leave();
    return err;
}
