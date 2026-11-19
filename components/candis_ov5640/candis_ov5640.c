/* SPDX-License-Identifier: Apache-2.0 */
/* Candis validation fixture OV5640 DVP route. */
#include <fcntl.h>
#include <unistd.h>
#include "bsp/candis_ov5640.h"
#include "bsp/candis_fixture.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_video_device.h"
#include "esp_video_init.h"

static const char *TAG = "candis_ov5640";
static bool s_clock_timer;
static bool s_clock_channel;
static bool s_power;
static bool s_video;

static esp_err_t start_camera(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(candis_fixture_camera_acquire(&bus), TAG, "power camera seat");
    s_power = true;
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
    const esp_video_init_dvp_config_t dvp = {
        .sccb_config = {.init_sccb = false, .i2c_handle = bus, .freq = 100000},
        .reset_pin = GPIO_NUM_39, .pwdn_pin = GPIO_NUM_40,
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

esp_err_t candis_ov5640_start(void)
{
    if (s_power || s_clock_timer || s_video) {
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
    if (s_power) {
        ESP_RETURN_ON_ERROR(candis_fixture_camera_release(), TAG, "release camera power");
        s_power = false;
    }
    return ESP_OK;
}
