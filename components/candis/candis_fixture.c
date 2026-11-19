/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2 with the validation fixture OV5640 connector. */
#include "bsp/candis_fixture.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "esp_check.h"

#define CAMERA_RESET GPIO_NUM_39
#define CAMERA_POWER_DOWN GPIO_NUM_40

static const char *TAG = "candis_fixture";
static candis_power_t s_camera[] = {
    {.rail = CANDIS_RAIL_FIXTURE_CAMERA_IO},
    {.rail = CANDIS_RAIL_FIXTURE_CAMERA_ANALOG},
    {.rail = CANDIS_RAIL_FIXTURE_CAMERA_CORE},
};
static bool s_pwdn;
static bool s_reset;

static esp_err_t assert_camera_reset(void)
{
    const gpio_config_t pwdn = {
        .pin_bit_mask = 1ULL << CAMERA_POWER_DOWN, .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_POWER_DOWN, 1), TAG, "release PWDN to DOVDD pull-up");
    ESP_RETURN_ON_ERROR(gpio_config(&pwdn), TAG, "configure PWDN");
    s_pwdn = true;
    const gpio_config_t reset = {.pin_bit_mask = 1ULL << CAMERA_RESET, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_RESET, 0), TAG, "assert RESET before driving pin");
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "configure RESET");
    s_reset = true;
    return ESP_OK;
}

static esp_err_t release_camera(void)
{
    if (s_pwdn || s_reset) {
        ESP_RETURN_ON_ERROR(assert_camera_reset(), TAG, "isolate sensor");
    }
    for (int i = 2; i >= 0; --i) {
        ESP_RETURN_ON_ERROR(candis_power_release(&s_camera[i]), TAG, "release sensor rail");
    }
    if (s_reset) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(CAMERA_RESET), TAG, "release RESET pin");
        s_reset = false;
    }
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(CAMERA_POWER_DOWN), TAG, "release PWDN pin");
        s_pwdn = false;
    }
    return ESP_OK;
}

esp_err_t candis_fixture_camera_acquire(i2c_master_bus_handle_t *bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    *bus = NULL;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pwdn || s_reset || s_camera[0].bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    static const unsigned delay_ms[] = {2, 2, 5};
    esp_err_t err = assert_camera_reset();
    for (size_t i = 0; err == ESP_OK && i < sizeof(s_camera) / sizeof(s_camera[0]); ++i) {
        err = candis_power_acquire(&s_camera[i], true, delay_ms[i]);
    }
    if (err == ESP_OK) {
        *bus = s_camera[0].bus;
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_camera());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_fixture_camera_release(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = release_camera();
    candis_control_leave();
    return err;
}
