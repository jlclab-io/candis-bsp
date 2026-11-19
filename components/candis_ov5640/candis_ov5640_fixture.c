/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2 with the validation fixture. */
#include "bsp/candis.h"
#include "bsp/candis_ov5640.h"
#include "bsp/candis_power.h"
#include "esp_check.h"

static const char *TAG = "candis_ov5640";
static i2c_master_bus_handle_t s_bus;
static candis_expansion_ldo_handle_t s_supplies[3];

static esp_err_t acquire_fixture(void *context, i2c_master_bus_handle_t *bus)
{
    (void)context;
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_bus), TAG, "acquire Candis SCCB bus");
    *bus = s_bus;
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO1, 2800, 2, &s_supplies[0]),
                        TAG, "enable DOVDD");
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO4, 2800, 2, &s_supplies[1]),
                        TAG, "enable AVDD");
    return candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &s_supplies[2]);
}

static esp_err_t release_fixture(void *context)
{
    (void)context;
    for (size_t i = sizeof(s_supplies) / sizeof(s_supplies[0]); i > 0; --i) {
        if (s_supplies[i - 1]) {
            ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(s_supplies[i - 1]), TAG, "release camera supply");
            s_supplies[i - 1] = NULL;
        }
    }
    if (s_bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release(s_bus), TAG, "release SCCB bus");
        s_bus = NULL;
    }
    return ESP_OK;
}

candis_ov5640_config_t candis_ov5640_fixture_config(void)
{
    return (candis_ov5640_config_t) {
        .data = {GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_49,
                 GPIO_NUM_50, GPIO_NUM_51, GPIO_NUM_52, GPIO_NUM_53},
        .pclk = GPIO_NUM_54,
        .vsync = GPIO_NUM_56,
        .hsync = GPIO_NUM_57,
        .reset = GPIO_NUM_39,
        .pwdn = GPIO_NUM_40,
        .pwdn_open_drain = true, /* R29 pulls PWDN up to the sensor's 2.8 V DOVDD. */
        .xclk = GPIO_NUM_55,
        .xclk_hz = 24000000,
        .xclk_timer = LEDC_TIMER_1,
        .xclk_channel = LEDC_CHANNEL_1,
        .sccb_scl = GPIO_NUM_NC,
        .sccb_sda = GPIO_NUM_NC,
        .sccb_hz = 100000,
        .acquire = acquire_fixture,
        .release = release_fixture,
    };
}

esp_err_t candis_ov5640_start(void)
{
    const candis_ov5640_config_t config = candis_ov5640_fixture_config();
    return candis_ov5640_start_with_config(&config);
}
