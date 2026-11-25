/* SPDX-License-Identifier: Apache-2.0 */
/* Candis Dock hardware revisions 1.1 and 1.2. */
#include <stdatomic.h>
#include "bsp/candis_dock.h"
#include "bsp/candis_baseboard_bus.h"
#include "bsp/candis_power.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "sdkconfig.h"

#define DOCK_EXP_I2C          I2C_NUM_1
#define DOCK_EXP_SCL          GPIO_NUM_58
#define DOCK_EXP_SDA          GPIO_NUM_59
#define DOCK_EEPROM_ADDR      0x50
#define DOCK_EEPROM_BYTES     256
#define DOCK_SD_CLK           GPIO_NUM_24
#define DOCK_SD_CMD           GPIO_NUM_25
#define DOCK_SD_D0            GPIO_NUM_20
#define DOCK_SD_D1            GPIO_NUM_21
#define DOCK_SD_D2            GPIO_NUM_22
#define DOCK_SD_D3            GPIO_NUM_23
#define DOCK_SD_CD            GPIO_NUM_39

static const char *TAG = "candis_dock";
static atomic_flag s_control = ATOMIC_FLAG_INIT;
static candis_expansion_power_handle_t s_power;
static i2c_master_bus_handle_t s_bus;
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

static esp_err_t check_uart_released(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    if (CONFIG_ESP_CONSOLE_UART_TX_GPIO == DOCK_EXP_SCL ||
        CONFIG_ESP_CONSOLE_UART_TX_GPIO == DOCK_EXP_SDA ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == DOCK_EXP_SCL ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == DOCK_EXP_SDA) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return uart_is_driver_installed(UART_NUM_0) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t open_bus(void)
{
    if (s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(check_uart_released(), TAG, "release UART0 before Dock I2C");
    const i2c_master_bus_config_t config = {
        .i2c_port = DOCK_EXP_I2C,
        .scl_io_num = DOCK_EXP_SCL,
        .sda_io_num = DOCK_EXP_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    return i2c_new_master_bus(&config, &s_bus);
}

static esp_err_t close_bus(void)
{
    if (!s_bus) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_bus), TAG, "release Dock I2C");
    s_bus = NULL;
    return ESP_OK;
}

static esp_err_t configure_storage(void)
{
    candis_storage_config_t config = CANDIS_STORAGE_CONFIG_DEFAULT();
    config.slot.clk = DOCK_SD_CLK;
    config.slot.cmd = DOCK_SD_CMD;
    config.slot.d0 = DOCK_SD_D0;
    config.slot.d1 = DOCK_SD_D1;
    config.slot.d2 = DOCK_SD_D2;
    config.slot.d3 = DOCK_SD_D3;
    config.slot.cd = DOCK_SD_CD;
    config.slot.wp = SDMMC_SLOT_NO_WP;
    config.slot.width = 4;
    config.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    config.card_detect_active_low = true;
    config.use_dc1sw = true;
    return candis_storage_configure(&config);
}

static esp_err_t probe_on_open_bus(candis_baseboard_info_t *identity)
{
    candis_baseboard_info_t result;
    ESP_RETURN_ON_ERROR(candis_baseboard_probe_on_bus(s_bus, DOCK_EEPROM_ADDR, &result),
                        TAG, "probe Dock descriptor");
    const esp_err_t valid = validate_identity(&result);
    ESP_RETURN_ON_ERROR(valid, TAG, "validate Dock descriptor");
    *identity = result;
    return ESP_OK;
}

esp_err_t candis_dock_probe(candis_baseboard_info_t *identity)
{
    if (!identity) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus) {
        return probe_on_open_bus(identity);
    }
    ESP_RETURN_ON_ERROR(open_bus(), TAG, "open Dock I2C");
    const esp_err_t err = probe_on_open_bus(identity);
    const esp_err_t cleanup = close_bus();
    return err == ESP_OK ? cleanup : err;
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
    } else if (s_power || s_bus) {
        err = ESP_ERR_INVALID_STATE;
    } else {
        err = open_bus();
        if (err == ESP_OK) {
            err = candis_baseboard_probe_state_on_bus(CANDIS_BASEBOARD_BB, s_bus,
                                                      DOCK_EEPROM_ADDR, state, identity);
            const esp_err_t cleanup = close_bus();
            err = err == ESP_OK ? cleanup : err;
        }
    }
    atomic_flag_clear(&s_control);
    return err;
}

static esp_err_t release_power(void)
{
    s_initialized = false;
    s_identity = (candis_baseboard_info_t){0};
    candis_storage_reset_config();
    ESP_RETURN_ON_ERROR(close_bus(), TAG, "release Dock I2C");
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
        err = check_uart_released();
        if (err == ESP_OK) {
            err = candis_expansion_power_claim(manage_power, &s_power);
        }
        if (err == ESP_OK) {
            err = open_bus();
        }
        if (err == ESP_OK) {
            err = probe_on_open_bus(identity);
            if (err == ESP_OK) {
                err = configure_storage();
            }
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

esp_err_t candis_dock_read_eeprom(uint8_t *data, size_t length)
{
    if (!data || !length || length > DOCK_EEPROM_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (atomic_flag_test_and_set(&s_control)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_initialized && s_bus) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = DOCK_EEPROM_ADDR,
            .scl_speed_hz = 100000,
        };
        i2c_master_dev_handle_t device = NULL;
        err = i2c_master_bus_add_device(s_bus, &config, &device);
        if (err == ESP_OK) {
            const uint8_t offset = 0;
            err = i2c_master_transmit_receive(device, &offset, 1, data, length, 100);
            const esp_err_t cleanup = i2c_master_bus_rm_device(device);
            if (err == ESP_OK) {
                err = cleanup;
            }
        }
    }
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
    ESP_RETURN_ON_ERROR(validate_identity(identity), TAG, "validate Dock identity");
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return candis_storage_mount_with_config(path, config, card);
}

esp_err_t candis_dock_detect_start(const candis_baseboard_info_t *identity)
{
    ESP_RETURN_ON_ERROR(validate_identity(identity), TAG, "validate Dock identity");
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return candis_storage_detect_start(false);
}

esp_err_t candis_dock_probe_modules(const candis_baseboard_info_t *identity, candis_dock_module_scan_t *scan)
{
    if (!scan) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(validate_identity(identity), TAG, "validate Dock identity");
    if (!s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    static const uint8_t addresses[] = {0x51, 0x52};
    uint32_t responding;
    ESP_RETURN_ON_ERROR(candis_i2c_probe_addresses(s_bus, addresses, sizeof(addresses), &responding),
                        TAG, "probe Dock module addresses");
    *scan = (candis_dock_module_scan_t){
        .address_51 = (responding & 1) != 0,
        .address_52 = (responding & 2) != 0,
    };
    return ESP_OK;
}
