/* SPDX-License-Identifier: Apache-2.0 */
/* Hardware-independent FAT/SDMMC transport for Candis applications. */
#include <stdlib.h>
#include <string.h>
#include "bsp/candis_storage.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "candis_storage";
static struct {
    bool configured;
    candis_storage_config_t config;
} s_config;

static struct {
    sdmmc_card_t *card;
    char *path;
    candis_power_t power;
    sdmmc_slot_config_t slot;
    bool bsp_mount;
    bool host_owned;
    bool release_pins;
    bool restore_detect;
    bool detect_claimed;
    bool removed;
    bool uses_dc1sw;
} s_storage;

static struct {
    candis_power_t power;
    gpio_num_t pin;
    bool active_low;
    bool pin_claimed;
    bool ready;
    bool uses_dc1sw;
} s_detect;

static bool valid_slot(const sdmmc_slot_config_t *slot)
{
    if (!slot || (slot->width != 1 && slot->width != 4)) {
        return false;
    }
    if (slot->clk == GPIO_NUM_NC || slot->cmd == GPIO_NUM_NC || slot->d0 == GPIO_NUM_NC) {
        return false;
    }
    return slot->width == 1 ||
           (slot->d1 != GPIO_NUM_NC && slot->d2 != GPIO_NUM_NC && slot->d3 != GPIO_NUM_NC);
}

static esp_err_t set_detect_input(gpio_num_t pin, bool enable)
{
    if (pin == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << pin,
        .mode = enable ? GPIO_MODE_INPUT : GPIO_MODE_DISABLE,
        .pull_up_en = enable ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    return gpio_config(&input);
}

static bool detect_present(gpio_num_t pin, bool active_low)
{
    return (gpio_get_level(pin) == 0) == active_low;
}

static esp_err_t release_detect_locked(void)
{
    s_detect.ready = false;
    if (s_detect.pin_claimed) {
        ESP_RETURN_ON_ERROR(set_detect_input(s_detect.pin, false), TAG, "release card detect pin");
        s_detect.pin_claimed = false;
    }
    if (s_detect.uses_dc1sw) {
        ESP_RETURN_ON_ERROR(candis_power_release(&s_detect.power), TAG, "release card detect power");
        s_detect.uses_dc1sw = false;
    }
    return ESP_OK;
}

esp_err_t candis_storage_configure(const candis_storage_config_t *config)
{
    if (!config || !valid_slot(&config->slot)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.path || s_storage.card || s_detect.pin_claimed || s_detect.uses_dc1sw) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_config.config = *config;
    s_config.configured = true;
    candis_control_leave();
    return ESP_OK;
}

void candis_storage_reset_config(void)
{
    if (!candis_control_enter()) {
        return;
    }
    if (!s_storage.path && !s_storage.card && !s_detect.pin_claimed && !s_detect.uses_dc1sw) {
        memset(&s_config, 0, sizeof(s_config));
    }
    candis_control_leave();
}

esp_err_t candis_storage_detect_start(bool manage_power)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_config.configured || s_detect.pin_claimed || s_storage.path || s_storage.card) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_config.config.slot.cd == GPIO_NUM_NC || (manage_power && !s_config.config.use_dc1sw)) {
        candis_control_leave();
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_detect.pin = s_config.config.slot.cd;
    s_detect.active_low = s_config.config.card_detect_active_low;
    s_detect.power.rail = CANDIS_RAIL_EXPANSION;
    s_detect.uses_dc1sw = s_config.config.use_dc1sw;
    esp_err_t err = s_detect.uses_dc1sw ? candis_power_acquire(&s_detect.power, manage_power, 0) : ESP_OK;
    if (err == ESP_OK) {
        s_detect.pin_claimed = true;
        err = set_detect_input(s_detect.pin, true);
        s_detect.ready = err == ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_detect_locked());
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
    if (s_detect.ready) {
        *present = detect_present(s_detect.pin, s_detect.active_low);
        if (s_storage.card && !*present) {
            s_storage.removed = true;
        }
    }
    const bool ready = s_detect.ready;
    candis_control_leave();
    return ready ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t candis_storage_detect_stop(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = (s_storage.path || s_storage.card) ? ESP_ERR_INVALID_STATE : release_detect_locked();
    if (err == ESP_OK && s_storage.restore_detect && !s_storage.path && !s_storage.card) {
        s_storage.restore_detect = false;
        s_storage.bsp_mount = false;
    }
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

static esp_err_t release_sd_pins(const sdmmc_slot_config_t *slot)
{
    const gpio_num_t pins[6] = {slot->clk, slot->cmd, slot->d0, slot->d1, slot->d2, slot->d3};
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (pins[i] == GPIO_NUM_NC) {
            continue;
        }
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

static esp_err_t release_storage_locked(void)
{
    esp_err_t result = ESP_OK;
    if (s_storage.card) {
        result = esp_vfs_fat_sdcard_unmount(s_storage.path, s_storage.card);
        s_storage.card = NULL;
    }
    if (s_storage.host_owned) {
        const esp_err_t err = sdmmc_host_deinit();
        if (err == ESP_OK) {
            s_storage.host_owned = false;
        } else {
            return err;
        }
    }
    if (s_storage.release_pins) {
        const esp_err_t err = release_sd_pins(&s_storage.slot);
        if (err != ESP_OK) {
            return result == ESP_OK ? err : result;
        }
        s_storage.release_pins = false;
    }
    free(s_storage.path);
    s_storage.path = NULL;
    if (s_storage.restore_detect && !s_detect.ready) {
        s_detect.pin_claimed = true;
        const esp_err_t err = set_detect_input(s_detect.pin, true);
        s_detect.ready = err == ESP_OK;
        if (err != ESP_OK) {
            return result == ESP_OK ? err : result;
        }
        s_storage.restore_detect = false;
    }
    if (s_storage.detect_claimed) {
        const esp_err_t err = set_detect_input(s_storage.slot.cd, false);
        if (err != ESP_OK) {
            return result == ESP_OK ? err : result;
        }
        s_storage.detect_claimed = false;
    }
    if (s_storage.uses_dc1sw) {
        const esp_err_t err = candis_power_release(&s_storage.power);
        if (err != ESP_OK) {
            return result == ESP_OK ? err : result;
        }
        s_storage.uses_dc1sw = false;
    }
    s_storage.removed = false;
    s_storage.bsp_mount = false;
    return result;
}

static esp_err_t prepare_mount_locked(const sdmmc_slot_config_t *slot, bool manage_power, bool use_dc1sw)
{
    sdmmc_host_state_t state = {0};
    ESP_RETURN_ON_ERROR(sdmmc_host_get_state(&state), TAG, "query SDMMC host");
    if (state.host_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!use_dc1sw && manage_power) {
        return ESP_ERR_INVALID_ARG;
    }
    s_storage.uses_dc1sw = use_dc1sw;
    if (use_dc1sw) {
        s_storage.power.rail = CANDIS_RAIL_EXPANSION;
        ESP_RETURN_ON_ERROR(candis_power_acquire(&s_storage.power, manage_power, 20), TAG, "prepare SD supply");
    }
    if (slot->cd == GPIO_NUM_NC) {
        return ESP_OK;
    }
    if (!s_detect.ready) {
        s_detect.pin = slot->cd;
        s_detect.active_low = s_config.configured ? s_config.config.card_detect_active_low : true;
        s_storage.detect_claimed = true;
        ESP_RETURN_ON_ERROR(set_detect_input(slot->cd, true), TAG, "configure card detect");
    }
    return detect_present(slot->cd, s_detect.active_low) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t mount_internal(const char *base_path, const sdmmc_host_t *host_override,
                                const sdmmc_slot_config_t *slot_override,
                                const esp_vfs_fat_sdmmc_mount_config_t *mount_override,
                                bool manage_power, bool bsp_mount, sdmmc_card_t **card)
{
    if (!base_path || base_path[0] != '/' || base_path[1] == '\0' || !card) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4};
    if (mount_override) {
        mount = *mount_override;
    }
    if (mount.max_files <= 0 || mount.format_if_mount_failed) {
        return ESP_ERR_INVALID_ARG;
    }
    const sdmmc_slot_config_t *slot = slot_override;
    const bool use_dc1sw = s_config.configured && s_config.config.use_dc1sw;
    if (!slot) {
        if (!s_config.configured) {
            return ESP_ERR_INVALID_STATE;
        }
        slot = &s_config.config.slot;
    }
    if (!valid_slot(slot)) {
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_host_t host = host_override ? *host_override : (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    if (host.max_freq_khz <= 0) {
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    }
    host.init = claim_sdmmc_host;
    host.deinit_p = release_sdmmc_host;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.path || s_storage.card || ((s_detect.ready || s_detect.pin_claimed) && s_detect.pin != slot->cd)) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_storage.slot = *slot;
    s_storage.bsp_mount = bsp_mount;
    s_storage.path = strdup(base_path);
    esp_err_t err = s_storage.path ? prepare_mount_locked(slot, manage_power, use_dc1sw) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        sdmmc_slot_config_t selected = *slot;
        err = esp_vfs_fat_sdmmc_mount(s_storage.path, &host, &selected, &mount, &s_storage.card);
    }
    if (err == ESP_OK) {
        *card = s_storage.card;
        s_storage.removed = false;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_storage_locked());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_mount(const char *base_path, bool manage_power, sdmmc_card_t **card)
{
    const candis_storage_mount_config_t config = {.manage_power = manage_power};
    return candis_storage_mount_with_config(base_path, &config, card);
}

esp_err_t candis_storage_mount_with_config(const char *base_path,
                                           const candis_storage_mount_config_t *config,
                                           sdmmc_card_t **card)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0)
    const esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4, .read_only = config->read_only};
#else
    if (config->read_only) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4};
#endif
    return mount_internal(base_path, NULL, NULL, &mount, config->manage_power, false, card);
}

esp_err_t candis_storage_check_card(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_storage.card) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_storage.slot.cd != GPIO_NUM_NC && !detect_present(s_storage.slot.cd, s_detect.active_low)) {
        s_storage.removed = true;
    }
    const esp_err_t err = s_storage.removed ? ESP_ERR_NOT_FOUND : ESP_OK;
    candis_control_leave();
    return err;
}

esp_err_t candis_storage_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_storage.bsp_mount ? ESP_ERR_INVALID_STATE : release_storage_locked();
    candis_control_leave();
    return err;
}

esp_err_t bsp_sdcard_mount(void)
{
    sdmmc_card_t *card = NULL;
    const esp_vfs_fat_sdmmc_mount_config_t mount = {.max_files = 4};
    const bool manage_power = s_config.configured && s_config.config.use_dc1sw;
    return mount_internal(BSP_SD_MOUNT_POINT, NULL, NULL, &mount, manage_power, true, &card);
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    if (!candis_control_enter()) {
        return NULL;
    }
    sdmmc_card_t *card = NULL;
    if (s_storage.bsp_mount && s_storage.card) {
        if (s_storage.slot.cd != GPIO_NUM_NC &&
            !detect_present(s_storage.slot.cd, s_detect.active_low)) {
            s_storage.removed = true;
        }
        if (!s_storage.removed) {
            card = s_storage.card;
        }
    }
    candis_control_leave();
    return card;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_storage.bsp_mount ? release_storage_locked() : ESP_OK;
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
    if (s_config.configured) {
        *config = s_config.config.slot;
    } else {
        *config = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    }
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
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_card_t *card = NULL;
    const bool manage_power = s_config.configured && s_config.config.use_dc1sw;
    return mount_internal(BSP_SD_MOUNT_POINT, cfg->host, cfg->slot.sdmmc,
                          cfg->mount, manage_power, true, &card);
}

esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
