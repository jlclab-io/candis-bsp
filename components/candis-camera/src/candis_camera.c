/* SPDX-License-Identifier: Apache-2.0 */
/* Configurable ESP32-S31 DVP camera adapter. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "bsp/candis_camera.h"
#include "candis_camera_profile_internal.h"
#include "candis_camera_sensor.h"
#include "esp_check.h"
#include "esp_video_device.h"
#include "esp_video_init.h"

static const char *TAG = "candis_camera";
typedef struct {
    candis_camera_config_t config;
    const candis_camera_profile_t *profile;
    bool configured;
    bool resources;
    bool clock_timer;
    bool clock_channel;
    bool video;
    bool ready;
} camera_state_t;

static camera_state_t s_camera;

static bool add_pin(uint64_t *used, gpio_num_t pin, bool output)
{
    if ((unsigned)pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin) ||
        (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin))) {
        return false;
    }
    const uint64_t mask = UINT64_C(1) << pin;
    if (*used & mask) {
        return false;
    }
    *used |= mask;
    return true;
}

static bool valid_config(const candis_camera_config_t *config)
{
    uint64_t used = 0;
    for (unsigned i = 0; i < sizeof(config->data) / sizeof(config->data[0]); ++i) {
        if (!add_pin(&used, config->data[i], false)) {
            return false;
        }
    }
    if (!add_pin(&used, config->pclk, false) || !add_pin(&used, config->vsync, false) ||
        !add_pin(&used, config->hsync, false)) {
        return false;
    }
    if ((config->acquire || config->prepare) != (config->release != NULL) || !config->sccb_hz ||
        config->sccb_hz > 400000) {
        return false;
    }
    if (config->xclk != GPIO_NUM_NC &&
        (!add_pin(&used, config->xclk, true) ||
         config->xclk_hz < 6000000 || config->xclk_hz > 27000000 ||
         (unsigned)config->xclk_timer >= LEDC_TIMER_MAX ||
         (unsigned)config->xclk_channel >= LEDC_CHANNEL_MAX)) {
        return false;
    }
    if (config->sccb_scl == GPIO_NUM_NC && config->sccb_sda == GPIO_NUM_NC) {
        return config->sccb_bus || config->acquire;
    }
    return !config->sccb_bus && (unsigned)config->sccb_port < SOC_HP_I2C_NUM &&
           add_pin(&used, config->sccb_scl, true) && add_pin(&used, config->sccb_sda, true);
}

static esp_err_t start_clock(camera_state_t *camera)
{
    const candis_camera_config_t *config = &camera->config;
    if (config->xclk == GPIO_NUM_NC) {
        return ESP_OK;
    }
    const ledc_timer_config_t clock = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = config->xclk_timer,
        .clk_cfg = LEDC_AUTO_CLK, .duty_resolution = LEDC_TIMER_1_BIT, .freq_hz = config->xclk_hz,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "configure XCLK timer");
    camera->clock_timer = true;
    const ledc_channel_config_t output = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = config->xclk_channel,
        .timer_sel = config->xclk_timer, .gpio_num = config->xclk, .duty = 1,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "start XCLK");
    camera->clock_channel = true;
    return ESP_OK;
}

static esp_err_t stop_clock(camera_state_t *camera)
{
    const candis_camera_config_t *config = &camera->config;
    if (camera->clock_channel) {
        const ledc_channel_config_t output = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = config->xclk_channel, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_stop(LEDC_LOW_SPEED_MODE, config->xclk_channel, 0), TAG, "stop XCLK");
        ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "release XCLK channel");
        camera->clock_channel = false;
    }
    if (camera->clock_timer) {
        const ledc_timer_config_t clock = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = config->xclk_timer, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_pause(LEDC_LOW_SPEED_MODE, config->xclk_timer), TAG, "pause XCLK timer");
        ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "release XCLK timer");
        camera->clock_timer = false;
    }
    return ESP_OK;
}

static esp_err_t start_camera(camera_state_t *camera)
{
    const candis_camera_config_t *config = &camera->config;
    i2c_master_bus_handle_t bus = config->sccb_bus;
    if (config->acquire) {
        camera->resources = true;
        ESP_RETURN_ON_ERROR(config->acquire(config->context, &bus), TAG, "acquire camera resources");
    }
    ESP_RETURN_ON_FALSE((bus != NULL) == (config->sccb_scl == GPIO_NUM_NC),
                        ESP_ERR_INVALID_STATE, TAG, "SCCB bus does not match configuration");
    /* LEDC supports clocks such as 24 MHz, which need a fractional divisor on S31. */
    ESP_RETURN_ON_ERROR(start_clock(camera), TAG, "start sensor clock");
    if (config->prepare) {
        camera->resources = true;
        ESP_RETURN_ON_ERROR(config->prepare(config->context), TAG, "prepare camera module");
    }
    esp_video_init_dvp_config_t dvp = {
        .sccb_config = {.init_sccb = bus == NULL, .freq = config->sccb_hz},
        .reset_pin = GPIO_NUM_NC, .pwdn_pin = GPIO_NUM_NC,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .vsync_io = config->vsync, .de_io = config->hsync,
            .pclk_io = config->pclk, .xclk_io = GPIO_NUM_NC,
        },
    };
    memcpy(dvp.dvp_pin.data_io, config->data, sizeof(config->data));
    if (bus) {
        dvp.sccb_config.i2c_handle = bus;
    } else {
        dvp.sccb_config.i2c_config.port = config->sccb_port;
        dvp.sccb_config.i2c_config.scl_pin = config->sccb_scl;
        dvp.sccb_config.i2c_config.sda_pin = config->sccb_sda;
    }
    const esp_video_init_config_t video_config = {.dvp = &dvp};
    ESP_RETURN_ON_ERROR(esp_video_init_with_flags(&video_config, ESP_VIDEO_INIT_FLAGS_DVP), TAG, "init DVP video");
    camera->video = true;
    const int fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_FALSE(close(fd) == 0, ESP_FAIL, TAG, "close DVP probe");
    camera->ready = true;
    return ESP_OK;
}

esp_err_t candis_camera_init(const candis_camera_config_t *config)
{
    if (s_camera.configured) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config || !valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    const candis_camera_profile_t *expected = NULL;
    if (config->validate_sensor) {
        expected = candis_camera_profile_get(config->expected_sensor);
        if (!expected) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!(expected->status_flags & CANDIS_CAMERA_PROFILE_BUILD_ENABLED)) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    s_camera.config = *config;
    s_camera.configured = true;
    esp_err_t err = start_camera(&s_camera);
    if (err == ESP_OK && expected) {
        const candis_camera_profile_t *detected;
        err = candis_camera_get_active_profile(&detected);
        if (err == ESP_OK && detected != expected) {
            ESP_LOGE(TAG, "expected %s, detected %s", expected->model, detected->model);
            err = ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_stop());
    }
    return err;
}

const char *candis_camera_device(void)
{
    return s_camera.ready ? ESP_VIDEO_DVP_DEVICE_NAME : NULL;
}

esp_err_t candis_camera_get_active_profile(const candis_camera_profile_t **profile)
{
    if (!profile) {
        return ESP_ERR_INVALID_ARG;
    }
    *profile = NULL;
    if (!s_camera.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_camera.profile) {
        esp_cam_sensor_id_t id;
        ESP_RETURN_ON_ERROR(candis_camera_read_sensor_id(&id), TAG, "read detected sensor ID");
        s_camera.profile = candis_camera_profile_find_id(&id);
    }
    *profile = s_camera.profile;
    return s_camera.profile ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t stop_camera(camera_state_t *camera)
{
    camera->ready = false;
    if (camera->video) {
        ESP_RETURN_ON_ERROR(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP), TAG, "release DVP video");
        camera->video = false;
    }
    ESP_RETURN_ON_ERROR(stop_clock(camera), TAG, "release XCLK");
    if (camera->resources) {
        ESP_RETURN_ON_ERROR(camera->config.release(camera->config.context), TAG, "release camera resources");
    }
    *camera = (camera_state_t){0};
    return ESP_OK;
}

esp_err_t candis_camera_stop(void)
{
    const esp_err_t result = candis_camera_stream_stop();
    if (candis_camera_stream_is_open()) {
        return result;
    }
    const esp_err_t cleanup = stop_camera(&s_camera);
    return result == ESP_OK ? cleanup : result;
}
