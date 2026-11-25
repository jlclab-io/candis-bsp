/* SPDX-License-Identifier: Apache-2.0 */
/* Application-owned Candis camera wiring and resource adapter. */
#include "bsp/candis.h"
#include "bsp/candis_baseboard.h"
#include "bsp/candis_power.h"
#include "camera_fixture.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "candis_camera";

static esp_err_t expansion_uart_released(void)
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

typedef struct {
    i2c_master_bus_handle_t bus;
    candis_expansion_ldo_handle_t supplies[3];
    candis_expansion_power_handle_t dc1sw;
    bool reset_claimed;
    bool pwdn_claimed;
} camera_resource_context_t;

static camera_resource_context_t s_fixture_resources;
static camera_resource_context_t s_dock_resources;

#define FIXTURE_RESET GPIO_NUM_39
#ifdef CONFIG_CANDIS_CAMERA_FIXTURE_PWDN_GPIO
#define FIXTURE_PWDN GPIO_NUM_40
#else
#define FIXTURE_PWDN GPIO_NUM_NC
#endif

static const candis_camera_config_t s_dvp_wiring = {
    .data = {GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_49,
             GPIO_NUM_50, GPIO_NUM_51, GPIO_NUM_52, GPIO_NUM_53},
    .pclk = GPIO_NUM_54,
    .vsync = GPIO_NUM_56,
    .hsync = GPIO_NUM_57,
    .xclk = GPIO_NUM_55,
    .xclk_timer = LEDC_TIMER_1,
    .xclk_channel = LEDC_CHANNEL_1,
    .sccb_scl = GPIO_NUM_NC,
    .sccb_sda = GPIO_NUM_NC,
    .sccb_hz = 100000,
    .validate_sensor = true,
};

static esp_err_t release_supplies(camera_resource_context_t *resources)
{
    for (size_t index = sizeof(resources->supplies) / sizeof(resources->supplies[0]); index > 0; --index) {
        candis_expansion_ldo_handle_t *handle = &resources->supplies[index - 1];
        if (*handle) {
            ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(*handle), TAG, "release camera supply");
            *handle = CANDIS_EXPANSION_LDO_HANDLE_NONE;
        }
    }
    return ESP_OK;
}

static esp_err_t acquire_dock_resources(void *context, i2c_master_bus_handle_t *bus)
{
    camera_resource_context_t *resources = context;
    if (!resources || !bus) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(expansion_uart_released(), TAG, "release UART0 before using expansion I2C");
#ifdef CONFIG_CANDIS_CAMERA_DOCK_V12_MANAGE_SUPPLIES
    ESP_RETURN_ON_ERROR(candis_expansion_power_claim(true, &resources->dc1sw), TAG,
                        "claim Dock DC1SW");
#endif
#ifdef CONFIG_CANDIS_CAMERA_DOCK_V12_ENABLE_BLDO1
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO1,
                        CONFIG_CANDIS_CAMERA_DOCK_V12_BLDO1_MV, 2, &resources->supplies[0]),
                        TAG, "enable camera BLDO1");
#endif
#ifdef CONFIG_CANDIS_CAMERA_DOCK_V12_ENABLE_ALDO3
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3,
                        CONFIG_CANDIS_CAMERA_DOCK_V12_ALDO3_MV, 5, &resources->supplies[1]),
                        TAG, "enable camera ALDO3");
#endif
#ifdef CONFIG_CANDIS_CAMERA_DOCK_V12_ENABLE_ALDO4
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO4,
                        CONFIG_CANDIS_CAMERA_DOCK_V12_ALDO4_MV, 2, &resources->supplies[2]),
                        TAG, "enable camera ALDO4");
#endif
    if (resources->supplies[0] || resources->supplies[1] || resources->supplies[2]) {
        vTaskDelay(pdMS_TO_TICKS(5) + 1);
    }

    if (!resources->bus) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_1,
            .scl_io_num = GPIO_NUM_58,
            .sda_io_num = GPIO_NUM_59,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
        };
        ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &resources->bus), TAG,
                            "create Dock expansion I2C");
    }
    *bus = resources->bus;
    return ESP_OK;
}

static esp_err_t release_dock_resources(void *context)
{
    camera_resource_context_t *resources = context;
    if (!resources) {
        return ESP_ERR_INVALID_ARG;
    }
    if (resources->bus) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(resources->bus), TAG, "release Dock expansion I2C");
        resources->bus = NULL;
    }
    ESP_RETURN_ON_ERROR(release_supplies(resources), TAG, "release camera supplies");
    if (resources->dc1sw) {
        return candis_expansion_power_unclaim(&resources->dc1sw);
    }
    return ESP_OK;
}

static esp_err_t claim_fixture_pin(gpio_num_t pin, gpio_mode_t mode, unsigned level, bool *claimed)
{
    if (pin == GPIO_NUM_NC) {
        return ESP_OK;
    }
    const gpio_config_t config = {.pin_bit_mask = UINT64_C(1) << pin, .mode = mode};
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level), TAG, "set initial fixture control level");
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure fixture control pin");
    *claimed = true;
    return ESP_OK;
}

static esp_err_t release_fixture_pin(gpio_num_t pin, bool *claimed)
{
    if (*claimed) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(pin), TAG, "release fixture control pin");
        ESP_RETURN_ON_ERROR(gpio_pullup_dis(pin), TAG, "disable pull-up to unpowered camera");
        *claimed = false;
    }
    return ESP_OK;
}

static esp_err_t acquire_fixture_resources(void *context, i2c_master_bus_handle_t *bus)
{
    camera_resource_context_t *resources = context;
    if (!resources || !bus) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The fixture pulls PWDN up to the sensor's DOVDD. */
    ESP_RETURN_ON_ERROR(claim_fixture_pin(FIXTURE_PWDN, GPIO_MODE_OUTPUT_OD, 1, &resources->pwdn_claimed),
                        TAG, "assert fixture PWDN");
    ESP_RETURN_ON_ERROR(claim_fixture_pin(FIXTURE_RESET, GPIO_MODE_OUTPUT, 0, &resources->reset_claimed),
                        TAG, "assert fixture RESET");
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&resources->bus), TAG, "acquire Candis SCCB bus");
    *bus = resources->bus;
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(
                            CANDIS_EXPANSION_BLDO1, 2800, 2, &resources->supplies[0]),
                        TAG, "enable fixture DOVDD");
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(
                            CANDIS_EXPANSION_ALDO4, 2800, 2, &resources->supplies[1]),
                        TAG, "enable fixture AVDD");
    return candis_expansion_ldo_acquire(
        CANDIS_EXPANSION_ALDO3, 1500, 5, &resources->supplies[2]);
}

static esp_err_t prepare_fixture(void *context)
{
    const camera_resource_context_t *resources = context;
    vTaskDelay(pdMS_TO_TICKS(5) + 1);
    if (resources->pwdn_claimed) {
        ESP_RETURN_ON_ERROR(gpio_set_level(FIXTURE_PWDN, 0), TAG, "release fixture PWDN");
    }
    vTaskDelay(pdMS_TO_TICKS(2) + 1);
    return gpio_set_level(FIXTURE_RESET, 1);
}

static esp_err_t release_fixture_resources(void *context)
{
    camera_resource_context_t *resources = context;
    if (!resources) {
        return ESP_ERR_INVALID_ARG;
    }
    if (resources->pwdn_claimed) {
        ESP_RETURN_ON_ERROR(gpio_set_level(FIXTURE_PWDN, 1), TAG, "assert fixture PWDN");
    }
    if (resources->reset_claimed) {
        ESP_RETURN_ON_ERROR(gpio_set_level(FIXTURE_RESET, 0), TAG, "assert fixture RESET");
    }
    ESP_RETURN_ON_ERROR(release_supplies(resources), TAG, "release fixture supplies");
    ESP_RETURN_ON_ERROR(release_fixture_pin(FIXTURE_RESET, &resources->reset_claimed), TAG, "release fixture RESET");
    ESP_RETURN_ON_ERROR(release_fixture_pin(FIXTURE_PWDN, &resources->pwdn_claimed), TAG, "release fixture PWDN");
    if (resources->bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release(resources->bus), TAG, "release fixture SCCB bus");
        resources->bus = NULL;
    }
    return ESP_OK;
}

candis_camera_config_t camera_fixture_config(void)
{
    candis_camera_config_t config = s_dvp_wiring;
    config.expected_sensor = CANDIS_CAMERA_SENSOR_OV5640;
    config.xclk_hz = candis_camera_profile_get(config.expected_sensor)->xclk_hz;
    config.acquire = acquire_fixture_resources;
    config.prepare = prepare_fixture;
    config.release = release_fixture_resources;
    config.context = &s_fixture_resources;
    return config;
}

esp_err_t camera_dock_v12_init(void)
{
    const candis_camera_profile_t *profile = NULL;
    for (size_t i = 0; i < candis_camera_profile_count(); ++i) {
        const candis_camera_profile_t *candidate = candis_camera_profile_at(i);
        if (candidate->status_flags & CANDIS_CAMERA_PROFILE_BUILD_ENABLED) {
            ESP_RETURN_ON_FALSE(!profile, ESP_ERR_INVALID_STATE, TAG, "select one DVP detector for Dock");
            profile = candidate;
        }
    }
    ESP_RETURN_ON_FALSE(profile, ESP_ERR_NOT_SUPPORTED, TAG, "enable a DVP detector for Dock");

    candis_camera_config_t config = s_dvp_wiring;
    config.expected_sensor = profile->sensor;
    config.xclk_hz = profile->xclk_hz;
    config.acquire = acquire_dock_resources;
    config.release = release_dock_resources;
    config.context = &s_dock_resources;
    return candis_camera_init(&config);
}
