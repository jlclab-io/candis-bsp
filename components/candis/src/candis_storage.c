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
static candis_power_t s_sd_feature = {.rail = CANDIS_RAIL_EXPANSION};
static bool s_sd_feature_ready;
static struct {
    sdmmc_card_t *card;
    char *path;
    candis_power_t power;
    candis_storage_profile_t profile;
    bool bsp_mount;
    bool host_owned;
    bool release_pins;
    bool restore_detect;
    bool detect_claimed;
    bool removed;
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

static esp_err_t require_dock(void)
{
    candis_baseboard_info_t dock;
    ESP_RETURN_ON_ERROR(candis_baseboard_probe_locked(&dock), TAG, "identify Dock");
    return dock.kind == CANDIS_BASEBOARD_BB ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_feature_enable(bsp_feature_t feature, bool enable)
{
    if (feature != BSP_FEATURE_SD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (!enable) {
        s_sd_feature_ready = false;
        err = candis_power_release(&s_sd_feature);
    } else if (!s_sd_feature_ready) {
        if (s_sd_feature.bus) {
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = candis_expansion_i2c_check();
            if (err == ESP_OK) {
                err = candis_power_acquire(&s_sd_feature, true, 20);
            }
            if (err == ESP_OK) {
                err = require_dock();
            }
            s_sd_feature_ready = err == ESP_OK;
            if (err != ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(candis_power_release(&s_sd_feature));
            }
        }
    }
    candis_control_leave();
    return err;
}

static esp_err_t set_detect_input(candis_storage_profile_t profile, bool enable)
{
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << detect_pin(profile),
        .mode = enable ? GPIO_MODE_INPUT : GPIO_MODE_DISABLE,
        .pull_up_en = enable ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    return gpio_config(&input);
}

static esp_err_t configure_detect(void)
{
    s_detect.pin_claimed = true;
    const esp_err_t err = set_detect_input(s_detect.profile, true);
    s_detect.ready = err == ESP_OK;
    return err;
}

static esp_err_t release_detect(void)
{
    s_detect.ready = false;
    if (s_detect.pin_claimed) {
        /* Remove the MCU pull-up before releasing the expansion power reference. */
        ESP_RETURN_ON_ERROR(set_detect_input(s_detect.profile, false), TAG, "release card detect pin");
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
        if (s_storage.card && !*present) {
            s_storage.removed = true;
        }
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

static esp_err_t claim_sdmmc_host(void)
{
    sdmmc_host_state_t state = {0};
    ESP_RETURN_ON_ERROR(sdmmc_host_get_state(&state), TAG, "query SDMMC host");
    if (state.host_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = sdmmc_host_init();
    if (err == ESP_OK) {
        s_storage.host_owned = true;
        s_storage.release_pins = true;
        s_storage.restore_detect = s_detect.ready;
    }
    return err;
}

static esp_err_t release_sdmmc_host(int slot)
{
    const esp_err_t err = sdmmc_host_deinit_slot(slot);
    if (err == ESP_OK) {
        s_storage.host_owned = false;
    }
    return err;
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

static esp_err_t release_storage(void)
{
    esp_err_t unmount = ESP_OK;
    if (s_storage.card) {
        /* IDF consumes the card handle before returning its final VFS status. */
        unmount = esp_vfs_fat_sdcard_unmount(s_storage.path, s_storage.card);
        s_storage.card = NULL;
    }
    if (s_storage.host_owned) {
        /* FAT can request slot deinit even when slot initialization failed. */
        ESP_RETURN_ON_ERROR(sdmmc_host_deinit(), TAG, "release SDMMC controller");
        s_storage.host_owned = false;
    }
    if (s_storage.release_pins) {
        const esp_err_t pins = release_sd_pins();
        if (pins != ESP_OK) {
            return unmount == ESP_OK ? pins : unmount;
        }
        s_storage.release_pins = false;
    }
    free(s_storage.path);
    s_storage.path = NULL;
    if (s_storage.restore_detect) {
        const esp_err_t err = configure_detect();
        s_storage.restore_detect = false;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "restore card detect: %s", esp_err_to_name(err));
            if (unmount == ESP_OK) {
                unmount = err;
            }
        }
    }
    if (s_storage.detect_claimed) {
        ESP_RETURN_ON_ERROR(set_detect_input(s_storage.profile, false), TAG, "release mount card detect pin");
        s_storage.detect_claimed = false;
    }
    esp_err_t power = candis_power_release(&s_storage.power);
    if (power != ESP_OK) {
        ESP_LOGE(TAG, "release DC1SW: %s", esp_err_to_name(power));
    } else {
        s_storage.bsp_mount = false;
        s_storage.removed = false;
    }
    return unmount == ESP_OK ? power : unmount;
}

esp_err_t candis_storage_mount(const char *base_path, candis_storage_profile_t profile,
                               bool manage_power, sdmmc_card_t **card)
{
    const candis_storage_mount_config_t config = {.manage_power = manage_power};
    return candis_storage_mount_with_config(base_path, profile, &config, card);
}

static esp_err_t prepare_mount(bool manage_power)
{
    sdmmc_host_state_t state = {0};
    ESP_RETURN_ON_ERROR(sdmmc_host_get_state(&state), TAG, "query SDMMC host");
    if (state.host_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_storage.power, manage_power, 20), TAG, "prepare SD supply");
    if (s_storage.bsp_mount) {
        ESP_RETURN_ON_ERROR(require_dock(), TAG, "verify Dock before using GPIO39");
    }
    if (!s_detect.ready) {
        s_storage.detect_claimed = true;
        ESP_RETURN_ON_ERROR(set_detect_input(s_storage.profile, true), TAG, "configure card detect");
    }
    return gpio_get_level(detect_pin(s_storage.profile)) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t mount_with_config(const char *base_path, candis_storage_profile_t profile,
                                   const bsp_sdcard_cfg_t *config, bool manage_power, bool bsp_mount,
                                   sdmmc_card_t **card)
{
    if (!base_path || base_path[0] != '/' || base_path[1] == '\0' || !card ||
        !config || !valid_profile(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4};
    if (config->mount) {
        mount = *config->mount;
    }
    if (mount.max_files <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_host_t host;
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &host);
    sdmmc_slot_config_t slot;
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
    slot.cd = detect_pin(profile);
    const sdmmc_slot_config_t *requested = config->slot.sdmmc;
    if (mount.format_if_mount_failed ||
        (requested && (requested->clk != slot.clk || requested->cmd != slot.cmd ||
                       requested->d0 != slot.d0 || requested->d1 != slot.d1 ||
                       requested->d2 != slot.d2 || requested->d3 != slot.d3 ||
                       requested->cd != slot.cd || requested->wp != slot.wp ||
                       requested->width != slot.width || requested->flags != slot.flags))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (config->host) {
        if (config->host->slot != host.slot || config->host->max_freq_khz <= 0 ||
            config->host->max_freq_khz > host.max_freq_khz || config->host->flags != host.flags ||
            config->host->io_voltage != host.io_voltage || config->host->pwr_ctrl_handle) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        host.max_freq_khz = config->host->max_freq_khz;
        host.command_timeout_ms = config->host->command_timeout_ms;
    }
    host.init = claim_sdmmc_host;
    host.deinit_p = release_sdmmc_host;
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
    s_storage.bsp_mount = bsp_mount;
    s_storage.profile = profile;
    s_storage.power.rail = CANDIS_RAIL_EXPANSION;
    esp_err_t err = s_storage.path ? prepare_mount(manage_power) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        err = esp_vfs_fat_sdmmc_mount(s_storage.path, &host, &slot, &mount, &s_storage.card);
    }
    if (err == ESP_OK) {
        *card = s_storage.card;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_storage());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_mount_with_config(const char *base_path, candis_storage_profile_t profile,
                                           const candis_storage_mount_config_t *config, sdmmc_card_t **card)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4, .read_only = config->read_only};
    const bsp_sdcard_cfg_t options = {.mount = &mount};
    return mount_with_config(base_path, profile, &options, config->manage_power, false, card);
}

static esp_err_t check_card(void)
{
    if (!s_storage.card) {
        return ESP_ERR_INVALID_STATE;
    }
    if (gpio_get_level(detect_pin(s_storage.profile)) != 0) {
        s_storage.removed = true;
    }
    return s_storage.removed ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t candis_storage_check_card(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = check_card();
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_storage.bsp_mount ? ESP_ERR_INVALID_STATE : release_storage();
    candis_control_leave();
    return err;
}

esp_err_t bsp_sdcard_mount(void)
{
    bsp_sdcard_cfg_t config = {0};
    return bsp_sdcard_sdmmc_mount(&config);
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    if (!candis_control_enter()) {
        return NULL;
    }
    sdmmc_card_t *card = s_storage.bsp_mount && check_card() == ESP_OK ? s_storage.card : NULL;
    candis_control_leave();
    return card;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_storage.bsp_mount ? release_storage() : ESP_OK;
    candis_control_leave();
    return err;
}

void bsp_sdcard_get_sdmmc_host(const int slot, sdmmc_host_t *config)
{
    if (!config) {
        return;
    }
    *config = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    config->slot = slot;
    config->max_freq_khz = SDMMC_FREQ_DEFAULT;
}

void bsp_sdcard_sdmmc_get_slot(const int slot, sdmmc_slot_config_t *config)
{
    if (!config) {
        return;
    }
    *config = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    config->clk = CANDIS_SD_CLK;
    config->cmd = CANDIS_SD_CMD;
    config->d0 = CANDIS_SD_D0;
    config->d1 = CANDIS_SD_D1;
    config->d2 = CANDIS_SD_D2;
    config->d3 = CANDIS_SD_D3;
    config->cd = CANDIS_EXP_SD_CD;
    config->wp = SDMMC_SLOT_NO_WP;
    config->width = 4;
    config->flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    (void)slot;
}

void bsp_sdcard_get_sdspi_host(const int slot, sdmmc_host_t *config)
{
    if (!config) {
        return;
    }
    *config = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    config->slot = slot;
}

void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config)
{
    if (!config) {
        return;
    }
    *config = (sdspi_device_config_t){
        .host_id = spi_host,
        .gpio_cs = GPIO_NUM_NC,
        .gpio_cd = SDSPI_SLOT_NO_CD,
        .gpio_wp = SDSPI_SLOT_NO_WP,
        .gpio_int = GPIO_NUM_NC,
    };
}

esp_err_t bsp_sdcard_sdmmc_mount(bsp_sdcard_cfg_t *cfg)
{
    sdmmc_card_t *card = NULL;
    return mount_with_config(BSP_SD_MOUNT_POINT, CANDIS_STORAGE_EXPANSION, cfg, false, true, &card);
}

esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
