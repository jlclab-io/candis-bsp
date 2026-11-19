/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2 with the validation fixture OV5640 connector. */
#include <fcntl.h>
#include <unistd.h>
#include "bsp/candis_ov5640.h"
#include "bsp/candis.h"
#include "bsp/candis_power.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAMERA_RESET GPIO_NUM_39
#define CAMERA_PWDN GPIO_NUM_40

static const char *TAG = "candis_ov5640";
static bool s_clock_timer;
static bool s_clock_channel;
static bool s_reset;
static bool s_pwdn;
static bool s_video;
static i2c_master_bus_handle_t s_bus;
static candis_expansion_ldo_handle_t s_supplies[3];

static esp_err_t power_camera(void)
{
    const gpio_config_t pwdn = {
        .pin_bit_mask = 1ULL << CAMERA_PWDN, .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_PWDN, 1), TAG, "release PWDN to DOVDD pull-up");
    ESP_RETURN_ON_ERROR(gpio_config(&pwdn), TAG, "configure PWDN");
    s_pwdn = true;
    const gpio_config_t reset = {
        .pin_bit_mask = 1ULL << CAMERA_RESET, .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_RESET, 0), TAG, "assert RESET before driving pin");
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "configure RESET");
    s_reset = true;
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO1, 2800, 2, &s_supplies[0]),
                        TAG, "enable DOVDD");
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO4, 2800, 2, &s_supplies[1]),
                        TAG, "enable AVDD");
    return candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &s_supplies[2]);
}

static esp_err_t start_camera(void)
{
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&s_bus), TAG, "acquire SCCB bus");
    ESP_RETURN_ON_ERROR(power_camera(), TAG, "power camera seat");
    const ledc_timer_config_t clock = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_1,
        .clk_cfg = LEDC_AUTO_CLK, .duty_resolution = LEDC_TIMER_1_BIT, .freq_hz = 24000000,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "configure XCLK timer");
    s_clock_timer = true;
    const ledc_channel_config_t output = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1, .gpio_num = GPIO_NUM_55, .duty = 1,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "start 24 MHz XCLK");
    s_clock_channel = true;

    vTaskDelay(pdMS_TO_TICKS(5) + 1);
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_PWDN, 0), TAG, "release sensor power-down");
    vTaskDelay(pdMS_TO_TICKS(2) + 1);
    ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_RESET, 1), TAG, "release sensor reset");
    /* The OV5640 driver waits for its configured power-on delay before SCCB access. */
    const esp_video_init_dvp_config_t dvp = {
        .sccb_config = {.init_sccb = false, .i2c_handle = s_bus, .freq = 100000},
        .reset_pin = GPIO_NUM_NC, .pwdn_pin = GPIO_NUM_NC,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io = {GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_49,
                        GPIO_NUM_50, GPIO_NUM_51, GPIO_NUM_52, GPIO_NUM_53},
            .vsync_io = GPIO_NUM_56, .de_io = GPIO_NUM_57, .pclk_io = GPIO_NUM_54, .xclk_io = GPIO_NUM_NC,
        },
    };
    const esp_video_init_config_t config = {.dvp = &dvp};
    ESP_RETURN_ON_ERROR(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP), TAG, "init DVP video");
    s_video = true;
    const int fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    return close(fd) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t release_control_pin(gpio_num_t pin)
{
    ESP_RETURN_ON_ERROR(gpio_reset_pin(pin), TAG, "release sensor GPIO");
    /* The fixture already biases these lines. Avoid pulling up an unpowered sensor. */
    return gpio_pullup_dis(pin);
}

esp_err_t candis_ov5640_start(void)
{
    if (s_bus) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_camera();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_ov5640_stop());
    }
    return err;
}

const char *candis_ov5640_device(void)
{
    return s_video ? ESP_VIDEO_DVP_DEVICE_NAME : NULL;
}

esp_err_t candis_ov5640_stop(void)
{
    if (s_video) {
        ESP_RETURN_ON_ERROR(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP), TAG, "release DVP video");
        s_video = false;
    }
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_PWDN, 1), TAG, "assert PWDN");
    }
    if (s_reset) {
        ESP_RETURN_ON_ERROR(gpio_set_level(CAMERA_RESET, 0), TAG, "assert RESET");
    }
    if (s_clock_channel) {
        const ledc_channel_config_t output = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = LEDC_CHANNEL_1, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0), TAG, "stop XCLK");
        ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "release XCLK channel and pin");
        s_clock_channel = false;
    }
    if (s_clock_timer) {
        const ledc_timer_config_t clock = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = LEDC_TIMER_1, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1), TAG, "pause XCLK timer");
        ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "release XCLK timer");
        s_clock_timer = false;
    }
    for (size_t i = sizeof(s_supplies) / sizeof(s_supplies[0]); i > 0; --i) {
        if (s_supplies[i - 1]) {
            ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(s_supplies[i - 1]), TAG, "release camera supply");
            s_supplies[i - 1] = NULL;
        }
    }
    if (s_reset) {
        ESP_RETURN_ON_ERROR(release_control_pin(CAMERA_RESET), TAG, "release RESET pin");
        s_reset = false;
    }
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(release_control_pin(CAMERA_PWDN), TAG, "release PWDN pin");
        s_pwdn = false;
    }
    if (s_bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release(s_bus), TAG, "release SCCB bus");
        s_bus = NULL;
    }
    return ESP_OK;
}
