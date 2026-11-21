/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2. GPIO58/59 have UART0 as their boot-time function. */
#include "bsp/candis_baseboard.h"
#include "candis_internal.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "sdkconfig.h"

static const char *TAG = "candis_expansion";
static struct {
    candis_power_t power;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t eeprom;
} s_probe;

static esp_err_t release_probe(void)
{
    if (s_probe.eeprom) {
        ESP_RETURN_ON_ERROR(i2c_master_bus_rm_device(s_probe.eeprom), TAG, "remove baseboard EEPROM");
        s_probe.eeprom = NULL;
    }
    if (s_probe.bus) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_probe.bus), TAG, "release expansion I2C");
        s_probe.bus = NULL;
    }
    return candis_power_release(&s_probe.power);
}

esp_err_t candis_expansion_i2c_check(void)
{
    /* The default console may use UART0 without installing a UART driver. */
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    if (CONFIG_ESP_CONSOLE_UART_TX_GPIO == CANDIS_EXP_SCL ||
        CONFIG_ESP_CONSOLE_UART_TX_GPIO == CANDIS_EXP_SDA ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == CANDIS_EXP_SCL ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == CANDIS_EXP_SDA) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return uart_is_driver_installed(UART_NUM_0) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t open_probe(void)
{
    ESP_RETURN_ON_ERROR(candis_expansion_i2c_check(), TAG, "release UART0 before claiming expansion I2C");
    s_probe.power.rail = CANDIS_RAIL_EXPANSION;
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_probe.power, false, 0), TAG, "DC1SW required");
    const i2c_master_bus_config_t bus = {
        .i2c_port = CANDIS_EXP_I2C,
        .scl_io_num = CANDIS_EXP_SCL, .sda_io_num = CANDIS_EXP_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    return i2c_new_master_bus(&bus, &s_probe.bus);
}

static esp_err_t read_image(uint8_t *image)
{
    ESP_RETURN_ON_ERROR(open_probe(), TAG, "acquire expansion I2C");
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_probe.bus, CANDIS_EXP_EEPROM_ADDR, 30), TAG, "probe expansion EEPROM");
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CANDIS_EXP_EEPROM_ADDR, .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_probe.bus, &device, &s_probe.eeprom), TAG, "add EEPROM");
    const uint8_t address = 0;
    return i2c_master_transmit_receive(s_probe.eeprom, &address, 1, image, CANDIS_BASEBOARD_IMAGE_SIZE, 40);
}

esp_err_t candis_baseboard_probe_locked(candis_baseboard_info_t *info)
{
    ESP_RETURN_ON_ERROR(release_probe(), TAG, "finish previous probe cleanup");
    uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE];
    const esp_err_t err = read_image(image);
    const esp_err_t cleanup = release_probe();
    if (cleanup != ESP_OK) {
        return cleanup;
    }
    return err == ESP_OK ? candis_baseboard_decode(image, sizeof(image), info) : err;
}

esp_err_t candis_baseboard_probe(candis_baseboard_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = candis_baseboard_probe_locked(info);
    candis_control_leave();
    return err;
}

esp_err_t candis_baseboard_probe_state(candis_baseboard_kind_t expected,
                                       candis_capability_state_t *state,
                                       candis_baseboard_info_t *info)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (expected != CANDIS_BASEBOARD_BB) {
        *state = CANDIS_CAPABILITY_UNSUPPORTED;
        return ESP_OK;
    }
    candis_baseboard_info_t detected;
    const esp_err_t err = candis_baseboard_probe(&detected);
    if (err == ESP_ERR_NOT_FOUND) {
        *state = CANDIS_CAPABILITY_NOT_PRESENT;
        return ESP_OK;
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        *state = CANDIS_CAPABILITY_UNSUPPORTED;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (info) {
        *info = detected;
    }
    *state = CANDIS_CAPABILITY_PRESENT;
    return ESP_OK;
}

esp_err_t candis_expansion_probe_addresses(const uint8_t *addresses, size_t count, uint32_t *responding)
{
    if (!addresses || !count || count > 32 || !responding) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        if (addresses[i] < 0x08 || addresses[i] > 0x77) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t mask = 0;
    esp_err_t err = release_probe();
    if (err == ESP_OK) {
        err = open_probe();
        if (err == ESP_OK) {
            for (size_t i = 0; i < count; ++i) {
                const esp_err_t probe = i2c_master_probe(s_probe.bus, addresses[i], 30);
                if (probe == ESP_OK) {
                    mask |= UINT32_C(1) << i;
                } else if (probe != ESP_ERR_NOT_FOUND) {
                    err = probe;
                    break;
                }
            }
        }
        const esp_err_t cleanup = release_probe();
        if (err == ESP_OK) {
            err = cleanup;
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(cleanup);
        }
    }
    if (err == ESP_OK) {
        *responding = mask;
    }
    candis_control_leave();
    return err;
}
