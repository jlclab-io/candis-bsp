/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_baseboard.h"
#include "bsp/candis_baseboard_bus.h"
#include "bsp/candis_power.h"
#include "candis_example_usb.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "candis_baseboard";
enum { EXP_I2C = I2C_NUM_1, EEPROM_ADDR = 0x50 };
static candis_expansion_power_handle_t s_expansion_power;

static esp_err_t uart_released(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    if (CONFIG_ESP_CONSOLE_UART_TX_GPIO == GPIO_NUM_58 ||
        CONFIG_ESP_CONSOLE_UART_TX_GPIO == GPIO_NUM_59 ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == GPIO_NUM_58 ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == GPIO_NUM_59) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return uart_is_driver_installed(UART_NUM_0) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t testboard_i2c_open(i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_ERROR(uart_released(), TAG, "release UART0");
    const i2c_master_bus_config_t config = {
        .i2c_port = EXP_I2C,
        .scl_io_num = GPIO_NUM_58,
        .sda_io_num = GPIO_NUM_59,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    return i2c_new_master_bus(&config, bus);
}

static esp_err_t ensure_expansion_power(void)
{
    candis_power_state_t power;
    esp_err_t err = candis_power_read(&power);
    if (err != ESP_OK || power.dc1sw) {
        return err;
    }
    if (!s_expansion_power) {
        err = candis_expansion_power_claim(true, &s_expansion_power);
        if (err != ESP_OK) {
            const esp_err_t cleanup = candis_expansion_power_unclaim(&s_expansion_power);
            ESP_LOGW(TAG, "BASEBOARD_POWER claim=%s cleanup=%s", esp_err_to_name(err),
                     esp_err_to_name(cleanup));
            return err;
        }
    }
    err = candis_power_read(&power);
    return err == ESP_OK && power.dc1sw ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static void probe(void)
{
    esp_err_t err = ensure_expansion_power();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BASEBOARD_POWER %s", esp_err_to_name(err));
        return;
    }
    candis_power_state_t power;
    err = candis_power_read(&power);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BASEBOARD_POWER %s", esp_err_to_name(err));
        return;
    }
    if (!power.dc1sw) {
        ESP_LOGW(TAG, "BASEBOARD_SKIP DC1SW=off; supply the verified expansion domain before probing");
        return;
    }
    i2c_master_bus_handle_t bus;
    err = testboard_i2c_open(&bus);
    if (err == ESP_OK) {
        candis_baseboard_info_t board;
        err = candis_baseboard_probe_on_bus(bus, EEPROM_ADDR, &board);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "BASEBOARD_ID name=%s kind=%d hw_revision=%u format=%u",
                     board.name, board.kind, board.hardware_revision, board.format_revision);
        }
        const esp_err_t close = i2c_del_master_bus(bus);
        err = err == ESP_OK ? close : err;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BASEBOARD_PROBE %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_LOGI(TAG, "BASEBOARD_READY EEPROM=0x50 commands=P:probe");
    probe();
    for (;;) {
        char byte;
        if (candis_example_usb_read(&byte) && (byte == 'P' || byte == 'p')) {
            probe();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
