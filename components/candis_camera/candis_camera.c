/* SPDX-License-Identifier: Apache-2.0 */
/* Candis validation fixture OV5640 DVP route. */
#include <fcntl.h>
#include <unistd.h>
#include "bsp/candis_camera.h"
#include "bsp/candis_fixture.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_check.h"
#include "esp_video_device.h"
#include "esp_video_init.h"

static const char *TAG = "candis_camera";
static esp_cam_sensor_xclk_handle_t s_xclk;
static bool s_clock_on;
static bool s_power;
static bool s_video;

static esp_err_t start_camera(void)
{
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(candis_fixture_camera_acquire(&bus), TAG, "power camera seat");
    s_power = true;
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_LEDC, &s_xclk), TAG, "allocate sensor clock");
    const esp_cam_sensor_xclk_config_t clock = {
        .ledc_cfg = {
            .timer = LEDC_TIMER_1, .clk_cfg = LEDC_AUTO_CLK, .channel = LEDC_CHANNEL_1,
            .xclk_freq_hz = 24000000, .xclk_pin = GPIO_NUM_55,
        },
    };
    ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_start(s_xclk, &clock), TAG, "start 24 MHz XCLK");
    s_clock_on = true;
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

esp_err_t candis_camera_start(void)
{
    if (s_power || s_xclk || s_video) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_camera();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_stop());
    }
    return err;
}

const char *candis_camera_device(void)
{
    return s_video ? ESP_VIDEO_DVP_DEVICE_NAME : NULL;
}

esp_err_t candis_camera_stop(void)
{
    if (s_video) {
        ESP_RETURN_ON_ERROR(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP), TAG, "release DVP video");
        s_video = false;
    }
    if (s_clock_on) {
        ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_stop(s_xclk), TAG, "stop XCLK");
        s_clock_on = false;
    }
    if (s_xclk) {
        ESP_RETURN_ON_ERROR(esp_cam_sensor_xclk_free(s_xclk), TAG, "release XCLK");
        s_xclk = NULL;
    }
    if (s_power) {
        ESP_RETURN_ON_ERROR(candis_fixture_camera_release(), TAG, "release camera power");
        s_power = false;
    }
    return ESP_OK;
}
