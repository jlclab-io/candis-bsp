/* SPDX-License-Identifier: Apache-2.0 */
/* Candis Gamepad hardware revision 1.0. Inputs use the board's ALDO3 pull-ups. */
#include "bsp/candis_gamepad.h"
#include "bsp/candis_power.h"
#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "candis_gamepad";
static bool s_active;
static const gpio_num_t s_pins[] = {
    GPIO_NUM_35, GPIO_NUM_48, GPIO_NUM_49, GPIO_NUM_50, GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_40,
};

esp_err_t candis_gamepad_open(const candis_baseboard_info_t *identity)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_GAMEPAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_active) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_power_state_t power;
    ESP_RETURN_ON_ERROR(candis_power_read(&power), TAG, "read expansion power");
    ESP_RETURN_ON_FALSE(power.dc1sw && power.aldo3, ESP_ERR_INVALID_STATE, TAG, "both expansion rails are required");
    gpio_config_t config = {.mode = GPIO_MODE_INPUT};
    for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); ++i) {
        config.pin_bit_mask |= 1ULL << s_pins[i];
    }
    esp_err_t err = gpio_config(&config);
    s_active = err == ESP_OK;
    return err;
}

esp_err_t candis_gamepad_read(candis_gamepad_inputs_t *inputs)
{
    if (!inputs) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_active) {
        return ESP_ERR_INVALID_STATE;
    }
    *inputs = (candis_gamepad_inputs_t){
        .joystick_pressed = !gpio_get_level(GPIO_NUM_35),
        .encoder_a = gpio_get_level(GPIO_NUM_48),
        .encoder_b = gpio_get_level(GPIO_NUM_49),
        .encoder_pressed = !gpio_get_level(GPIO_NUM_50),
        .sw3_pressed = !gpio_get_level(GPIO_NUM_46),
        .sw4_pressed = !gpio_get_level(GPIO_NUM_47),
        .sw6_pressed = !gpio_get_level(GPIO_NUM_40),
    };
    return ESP_OK;
}

esp_err_t candis_gamepad_close(void)
{
    if (!s_active) {
        return ESP_OK;
    }
    for (size_t i = 0; i < sizeof(s_pins) / sizeof(s_pins[0]); ++i) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(s_pins[i]), TAG, "release Gamepad pin");
    }
    s_active = false;
    return ESP_OK;
}
