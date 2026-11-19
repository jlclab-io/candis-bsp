/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_dock.h"
#include "bsp/candis_gamepad.h"
#include "esp_log.h"

static const char *TAG = "candis_expansion";

static esp_err_t inspect_expansion(const candis_baseboard_info_t *identity)
{
    if (identity->kind == CANDIS_BASEBOARD_BB) {
        sdmmc_card_t *card;
        esp_err_t err = candis_dock_mount(identity, "/sdcard", &card);
        if (err != ESP_OK) {
            return err;
        }
        ESP_LOGI(TAG, "Dock card: %d kHz, width %u", card->real_freq_khz, 1U << card->log_bus_width);
        return candis_storage_unmount();
    }
    esp_err_t err = candis_gamepad_open(identity);
    if (err != ESP_OK) {
        return err;
    }
    candis_gamepad_inputs_t inputs;
    err = candis_gamepad_read(&inputs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Gamepad enc=%d/%d center=%d/%d switches=%d/%d/%d",
                 inputs.encoder_a, inputs.encoder_b, inputs.joystick_pressed, inputs.encoder_pressed,
                 inputs.sw3_pressed, inputs.sw4_pressed, inputs.sw6_pressed);
    }
    esp_err_t close = candis_gamepad_close();
    return err == ESP_OK ? close : err;
}

void app_main(void)
{
    candis_baseboard_info_t identity;
    esp_err_t err = candis_baseboard_probe(&identity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Baseboard %s, hardware=0x%04x", identity.name, identity.hardware_revision);
        err = inspect_expansion(&identity);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Expansion diagnostic: %s", esp_err_to_name(err));
    }
}
