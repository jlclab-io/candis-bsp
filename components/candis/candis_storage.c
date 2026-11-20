/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2, SDMMC routed through CN1. */
#include <stdlib.h>
#include <string.h>
#include "bsp/candis_storage.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "candis_storage";
static struct {
    sdmmc_card_t *card;
    char *path;
    candis_power_t power;
} s_storage;

static struct {
    candis_storage_profile_t profile;
    candis_power_t power;
    bool pin_claimed;
    bool ready;
} s_detect;

static bool valid_profile(candis_storage_profile_t profile)
{
    return profile == CANDIS_STORAGE_EXPANSION || profile == CANDIS_STORAGE_FIXTURE;
}

static gpio_num_t detect_pin(candis_storage_profile_t profile)
{
    return profile == CANDIS_STORAGE_FIXTURE ? CANDIS_FIXTURE_SD_CD : CANDIS_EXP_SD_CD;
}

static esp_err_t configure_detect(void)
{
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << detect_pin(s_detect.profile),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    s_detect.pin_claimed = true;
    const esp_err_t err = gpio_config(&input);
    s_detect.ready = err == ESP_OK;
    return err;
}

static esp_err_t release_detect(void)
{
    s_detect.ready = false;
    if (s_detect.pin_claimed) {
        /* Remove the MCU pull-up before releasing the expansion power reference. */
        const gpio_config_t disabled = {
            .pin_bit_mask = 1ULL << detect_pin(s_detect.profile),
            .mode = GPIO_MODE_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&disabled), TAG, "release card detect pin");
        s_detect.pin_claimed = false;
    }
    return candis_power_release(&s_detect.power);
}

esp_err_t candis_storage_detect_start(candis_storage_profile_t profile)
{
    if (!valid_profile(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.path || s_storage.power.bus || s_detect.pin_claimed || s_detect.power.bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_detect.profile = profile;
    s_detect.power.rail = CANDIS_RAIL_EXPANSION;
    esp_err_t err = candis_power_acquire(&s_detect.power, false, 0);
    if (err == ESP_OK) {
        err = configure_detect();
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_detect());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_detect_read(bool *present)
{
    if (!present) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool ready = s_detect.ready;
    if (ready) {
        *present = gpio_get_level(detect_pin(s_detect.profile)) == 0;
    }
    candis_control_leave();
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t candis_storage_detect_stop(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_detect.power.bus && (s_storage.path || s_storage.power.bus) ?
                          ESP_ERR_INVALID_STATE : release_detect();
    candis_control_leave();
    return err;
}

static esp_err_t mount_card(candis_storage_profile_t profile, bool read_only)
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
    slot.cd = detect_pin(profile);
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.width = 4;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    const esp_vfs_fat_mount_config_t config = {.max_files = 4, .read_only = read_only};
    return esp_vfs_fat_sdmmc_mount(s_storage.path, &host, &slot, &config, &s_storage.card);
}

static esp_err_t release_sd_pins(void)
{
    const gpio_num_t pins[] = {
        CANDIS_SD_CLK, CANDIS_SD_CMD, CANDIS_SD_D0, CANDIS_SD_D1, CANDIS_SD_D2, CANDIS_SD_D3,
    };
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        // The SD slot disables output but leaves the D3 reservation and internal pulls.
        esp_err_t err = gpio_reset_pin(pins[i]);
        if (err == ESP_OK) {
            err = gpio_pullup_dis(pins[i]);
        }
        if (result == ESP_OK) {
            result = err;
        }
    }
    return result;
}

static esp_err_t release_storage(bool restore_detect)
{
    esp_err_t unmount = ESP_OK;
    if (s_storage.card) {
        /* IDF consumes the card handle before returning its final VFS status. */
        unmount = esp_vfs_fat_sdcard_unmount(s_storage.path, s_storage.card);
        s_storage.card = NULL;
        const esp_err_t pins = release_sd_pins();
        if (unmount == ESP_OK) {
            unmount = pins;
        }
    }
    free(s_storage.path);
    s_storage.path = NULL;
    if (restore_detect && s_detect.ready) {
        const esp_err_t err = configure_detect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "restore card detect: %s", esp_err_to_name(err));
            if (unmount == ESP_OK) {
                unmount = err;
            }
        }
    }
    esp_err_t power = candis_power_release(&s_storage.power);
    if (power != ESP_OK) {
        ESP_LOGE(TAG, "release DC1SW: %s", esp_err_to_name(power));
    }
    return unmount == ESP_OK ? power : unmount;
}

esp_err_t candis_storage_mount(const char *base_path, candis_storage_profile_t profile,
                               bool manage_power, sdmmc_card_t **card)
{
    const candis_storage_mount_config_t config = {.manage_power = manage_power};
    return candis_storage_mount_with_config(base_path, profile, &config, card);
}

esp_err_t candis_storage_mount_with_config(const char *base_path, candis_storage_profile_t profile,
                                           const candis_storage_mount_config_t *config, sdmmc_card_t **card)
{
    if (!base_path || base_path[0] != '/' || base_path[1] == '\0' || !card ||
        !config || !valid_profile(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.path || s_storage.power.bus ||
        (s_detect.power.bus && (!s_detect.ready || profile != s_detect.profile))) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_detect.ready && gpio_get_level(detect_pin(profile)) != 0) {
        candis_control_leave();
        return ESP_ERR_NOT_FOUND;
    }
    s_storage.path = strdup(base_path);
    s_storage.power.rail = CANDIS_RAIL_EXPANSION;
    esp_err_t err = s_storage.path ? candis_power_acquire(&s_storage.power, config->manage_power, 20) : ESP_ERR_NO_MEM;
    const bool host_attempted = err == ESP_OK;
    if (host_attempted) {
        err = mount_card(profile, config->read_only);
    }
    if (err == ESP_OK) {
        *card = s_storage.card;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_storage(host_attempted));
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = release_storage(s_storage.card != NULL);
    candis_control_leave();
    return err;
}
