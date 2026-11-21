/* SPDX-License-Identifier: Apache-2.0 */
/* Candis Dock hardware revisions 1.1 and 1.2. */
#include <stdatomic.h>
#include "bsp/candis_dock.h"
#include "bsp/candis_power.h"

static atomic_flag s_control = ATOMIC_FLAG_INIT;
static candis_expansion_power_handle_t s_power;
static bool s_initialized;
static candis_baseboard_info_t s_identity;

static esp_err_t validate_identity(const candis_baseboard_info_t *identity)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB) {
        return ESP_ERR_INVALID_ARG;
    }
    return identity->hardware_revision == 0x0101 || identity->hardware_revision == 0x0102 ?
           ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t candis_dock_probe(candis_baseboard_info_t *identity)
{
    if (!identity) {
        return ESP_ERR_INVALID_ARG;
    }
    candis_baseboard_info_t result;
    const esp_err_t err = candis_baseboard_probe(&result);
    if (err != ESP_OK) {
        return err;
    }
    if (result.kind != CANDIS_BASEBOARD_BB) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *identity = result;
    return ESP_OK;
}

esp_err_t candis_dock_get_state(candis_capability_state_t *state, candis_baseboard_info_t *identity)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_flag_test_and_set(&s_control)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err;
    if (s_initialized) {
        *state = CANDIS_CAPABILITY_INITIALIZED;
        if (identity) {
            *identity = s_identity;
        }
        err = ESP_OK;
    } else if (s_power) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        err = candis_baseboard_probe_state(CANDIS_BASEBOARD_BB, state, identity);
    }
    atomic_flag_clear(&s_control);
    return err;
}

static esp_err_t release_power(void)
{
    s_initialized = false;
    s_identity = (candis_baseboard_info_t){0};
    return candis_expansion_power_unclaim(&s_power);
}

esp_err_t candis_dock_open(bool manage_power, candis_baseboard_info_t *identity)
{
    if (!identity) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_flag_test_and_set(&s_control)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_power) {
        err = candis_expansion_i2c_check();
        if (err == ESP_OK) {
            err = candis_expansion_power_claim(manage_power, &s_power);
        }
        if (err == ESP_OK) {
            err = candis_dock_probe(identity);
            if (err == ESP_OK) {
                s_identity = *identity;
                s_initialized = true;
            }
        }
        if (err != ESP_OK && s_power) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(release_power());
        }
    }
    atomic_flag_clear(&s_control);
    return err;
}

esp_err_t candis_dock_close(void)
{
    if (atomic_flag_test_and_set(&s_control)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = release_power();
    atomic_flag_clear(&s_control);
    return err;
}

esp_err_t candis_dock_get_speaker_channel(const candis_baseboard_info_t *identity,
                                         candis_dock_speaker_channel_t *channel)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_BB || !channel) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (identity->hardware_revision) {
    case 0x0101:
        *channel = CANDIS_DOCK_SPEAKER_LEFT;
        return ESP_OK;
    case 0x0102:
        *channel = CANDIS_DOCK_SPEAKER_RIGHT;
        return ESP_OK;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

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
    const esp_err_t err = validate_identity(identity);
    if (err != ESP_OK) {
        return err;
    }
    return candis_storage_mount_with_config(path, CANDIS_STORAGE_EXPANSION, config, card);
}

esp_err_t candis_dock_detect_start(const candis_baseboard_info_t *identity)
{
    const esp_err_t err = validate_identity(identity);
    return err == ESP_OK ? candis_storage_detect_start(CANDIS_STORAGE_EXPANSION) : err;
}

esp_err_t candis_dock_probe_modules(const candis_baseboard_info_t *identity, candis_dock_module_scan_t *scan)
{
    if (!scan) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = validate_identity(identity);
    if (err != ESP_OK) {
        return err;
    }
    static const uint8_t addresses[] = {0x51, 0x52};
    uint32_t responding;
    err = candis_expansion_probe_addresses(addresses, sizeof(addresses), &responding);
    if (err == ESP_OK) {
        *scan = (candis_dock_module_scan_t){
            .address_51 = (responding & 1) != 0,
            .address_52 = (responding & 2) != 0,
        };
    }
    return err;
}
