/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_gamepad.h"
#include "bsp/candis_power.h"
#include "candis_gamepad_filter.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_timer.h"

static const char *TAG = "candis_gamepad";

enum {
    JOYSTICK_X = GPIO_NUM_42,
    JOYSTICK_Y = GPIO_NUM_43,
    JOYSTICK_BUTTON = GPIO_NUM_35,
    ENCODER_A = GPIO_NUM_48,
    ENCODER_B = GPIO_NUM_49,
    ENCODER_BUTTON = GPIO_NUM_50,
    SW3 = GPIO_NUM_46,
    SW4 = GPIO_NUM_47,
    SW5 = GPIO_NUM_38,
    SW6 = GPIO_NUM_40,
};

static const gpio_num_t digital_pins[] = {JOYSTICK_BUTTON, ENCODER_A, ENCODER_B, ENCODER_BUTTON, SW3, SW4, SW6};

static struct {
    bool ready;
    uint64_t pins;
    adc_oneshot_unit_handle_t adc;
    adc_cali_handle_t calibration;
    adc_channel_t x;
    adc_channel_t y;
    candis_gamepad_config_t config;
    candis_gamepad_filter_t filter;
} s_gamepad;

static esp_err_t open_adc(bool calibrate)
{
    adc_unit_t x_unit, y_unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(JOYSTICK_X, &x_unit, &s_gamepad.x), TAG, "map X");
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(JOYSTICK_Y, &y_unit, &s_gamepad.y), TAG, "map Y");
    ESP_RETURN_ON_FALSE(x_unit == y_unit, ESP_ERR_NOT_SUPPORTED, TAG, "joystick ADC units differ");

    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = x_unit};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &s_gamepad.adc), TAG, "reserve joystick ADC");
    s_gamepad.pins |= (1ULL << JOYSTICK_X) | (1ULL << JOYSTICK_Y);
    const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_0, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_gamepad.adc, s_gamepad.x, &channel), TAG, "ADC X");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_gamepad.adc, s_gamepad.y, &channel), TAG, "ADC Y");
    if (!calibrate) {
        return ESP_OK;
    }
    const adc_cali_line_fitting_config_t calibration = {
        .unit_id = x_unit, .atten = channel.atten, .bitwidth = channel.bitwidth,
    };
    return adc_cali_create_scheme_line_fitting(&calibration, &s_gamepad.calibration);
}

esp_err_t candis_gamepad_open(const candis_baseboard_info_t *identity)
{
    const candis_gamepad_config_t config = CANDIS_GAMEPAD_CONFIG_DEFAULT();
    return candis_gamepad_open_with_config(identity, &config);
}

esp_err_t candis_gamepad_open_with_config(const candis_baseboard_info_t *identity,
                                         const candis_gamepad_config_t *config)
{
    if (!identity || identity->kind != CANDIS_BASEBOARD_GAMEPAD || !config ||
        (config->calibrate_axes && !config->axes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_gamepad.ready || s_gamepad.pins || s_gamepad.adc || s_gamepad.calibration) {
        return ESP_ERR_INVALID_STATE;
    }
    candis_power_state_t power;
    ESP_RETURN_ON_ERROR(candis_power_read(&power), TAG, "read expansion power");
    ESP_RETURN_ON_FALSE(power.dc1sw && power.aldo3, ESP_ERR_INVALID_STATE, TAG, "both expansion rails are required");

    for (size_t i = 0; i < sizeof(digital_pins) / sizeof(digital_pins[0]); ++i) {
        s_gamepad.pins |= 1ULL << digital_pins[i];
    }
    if (config->shared_sw5) {
        s_gamepad.pins |= 1ULL << SW5;
    }
    const gpio_config_t pins = {.mode = GPIO_MODE_INPUT, .pin_bit_mask = s_gamepad.pins};
    esp_err_t err = gpio_config(&pins);
    if (err == ESP_OK && config->axes) {
        err = open_adc(config->calibrate_axes);
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_gamepad_close());
        return err;
    }
    s_gamepad.config = *config;
    s_gamepad.filter = (candis_gamepad_filter_t){0};
    s_gamepad.ready = true;
    return ESP_OK;
}

esp_err_t candis_gamepad_read(candis_gamepad_inputs_t *inputs)
{
    if (!inputs) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_gamepad.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    *inputs = (candis_gamepad_inputs_t){
        .joystick_pressed = !gpio_get_level(JOYSTICK_BUTTON),
        .encoder_a = gpio_get_level(ENCODER_A),
        .encoder_b = gpio_get_level(ENCODER_B),
        .encoder_pressed = !gpio_get_level(ENCODER_BUTTON),
        .sw3_pressed = !gpio_get_level(SW3),
        .sw4_pressed = !gpio_get_level(SW4),
        .sw6_pressed = !gpio_get_level(SW6),
    };
    return ESP_OK;
}

esp_err_t candis_gamepad_poll(candis_gamepad_sample_t *sample)
{
    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    candis_gamepad_inputs_t pins;
    ESP_RETURN_ON_ERROR(candis_gamepad_read(&pins), TAG, "read inputs");
    candis_gamepad_sample_t result = {0};
    if (s_gamepad.adc) {
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_gamepad.adc, s_gamepad.x, &result.raw_x), TAG, "sample X");
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_gamepad.adc, s_gamepad.y, &result.raw_y), TAG, "sample Y");
        result.axes_valid = true;
        if (s_gamepad.calibration) {
            int x_mv, y_mv;
            if (adc_cali_chan_raw_to_voltage(s_gamepad.calibration, s_gamepad.x, result.raw_x, &x_mv) == ESP_OK &&
                adc_cali_chan_raw_to_voltage(s_gamepad.calibration, s_gamepad.y, result.raw_y, &y_mv) == ESP_OK) {
                result.voltage_valid = true;
                result.x_mv = x_mv;
                result.y_mv = y_mv;
            }
        }
    }
    const bool sw5 = s_gamepad.config.shared_sw5 && !gpio_get_level(SW5);
    const uint32_t raw = (pins.joystick_pressed ? CANDIS_GAMEPAD_JOYSTICK : 0) |
                         (pins.encoder_pressed ? CANDIS_GAMEPAD_ENCODER : 0) |
                         (pins.sw3_pressed ? CANDIS_GAMEPAD_SW3 : 0) |
                         (pins.sw4_pressed ? CANDIS_GAMEPAD_SW4 : 0) |
                         (sw5 ? CANDIS_GAMEPAD_SW5 : 0) |
                         (pins.sw6_pressed ? CANDIS_GAMEPAD_SW6 : 0);
    const uint32_t previous = s_gamepad.filter.initialized ? s_gamepad.filter.stable : raw;
    result.encoder_steps = candis_gamepad_filter_update(&s_gamepad.filter, raw,
                           (pins.encoder_a << 1) | pins.encoder_b, esp_timer_get_time(),
                           s_gamepad.config.debounce_ms * 1000U);
    result.buttons = s_gamepad.filter.stable;
    result.pressed = result.buttons & ~previous;
    result.released = previous & ~result.buttons;
    *sample = result;
    return ESP_OK;
}

esp_err_t candis_gamepad_close(void)
{
    s_gamepad.ready = false;
    if (s_gamepad.calibration) {
        ESP_RETURN_ON_ERROR(adc_cali_delete_scheme_line_fitting(s_gamepad.calibration), TAG, "release ADC calibration");
        s_gamepad.calibration = NULL;
    }
    if (s_gamepad.adc) {
        ESP_RETURN_ON_ERROR(adc_oneshot_del_unit(s_gamepad.adc), TAG, "release joystick ADC");
        s_gamepad.adc = NULL;
    }
    if (s_gamepad.pins) {
        const gpio_config_t pins = {.pin_bit_mask = s_gamepad.pins, .mode = GPIO_MODE_DISABLE};
        ESP_RETURN_ON_ERROR(gpio_config(&pins), TAG, "release Gamepad pins");
        s_gamepad.pins = 0;
    }
    return ESP_OK;
}
