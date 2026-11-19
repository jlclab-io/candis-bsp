/* SPDX-License-Identifier: Apache-2.0 */
/* ESP32-S31 OV5640 DVP adapter. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "bsp/candis_ov5640.h"
#include "esp_check.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_ov5640";
static bool s_clock_timer;
static bool s_clock_channel;
static bool s_reset;
static bool s_pwdn;
static bool s_video;
static bool s_ready;
static bool s_configured;
static bool s_resources;
static candis_ov5640_config_t s_config;

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

static bool valid_config(const candis_ov5640_config_t *config)
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
    const gpio_num_t outputs[] = {config->reset, config->pwdn, config->xclk};
    for (unsigned i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        if (outputs[i] != GPIO_NUM_NC && !add_pin(&used, outputs[i], true)) {
            return false;
        }
    }
    if ((config->acquire == NULL) != (config->release == NULL) || !config->sccb_hz ||
        config->sccb_hz > 400000) {
        return false;
    }
    if (config->xclk != GPIO_NUM_NC &&
        (config->xclk_hz < 6000000 || config->xclk_hz > 27000000 ||
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

static esp_err_t prepare_control_pin(gpio_num_t pin, gpio_mode_t mode, unsigned level, bool *claimed)
{
    if (pin == GPIO_NUM_NC) {
        return ESP_OK;
    }
    const gpio_config_t config = {.pin_bit_mask = UINT64_C(1) << pin, .mode = mode};
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level), TAG, "set initial control level");
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure camera control pin");
    *claimed = true;
    return ESP_OK;
}

static esp_err_t start_clock(void)
{
    if (s_config.xclk == GPIO_NUM_NC) {
        return ESP_OK;
    }
    const ledc_timer_config_t clock = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = s_config.xclk_timer,
        .clk_cfg = LEDC_AUTO_CLK, .duty_resolution = LEDC_TIMER_1_BIT, .freq_hz = s_config.xclk_hz,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "configure XCLK timer");
    s_clock_timer = true;
    const ledc_channel_config_t output = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .channel = s_config.xclk_channel,
        .timer_sel = s_config.xclk_timer, .gpio_num = s_config.xclk, .duty = 1,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "start XCLK");
    s_clock_channel = true;

    return ESP_OK;
}

static esp_err_t start_camera(void)
{
    const gpio_mode_t pwdn_mode = s_config.pwdn_open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(prepare_control_pin(s_config.pwdn, pwdn_mode, 1, &s_pwdn), TAG, "assert PWDN");
    ESP_RETURN_ON_ERROR(prepare_control_pin(s_config.reset, GPIO_MODE_OUTPUT, 0, &s_reset), TAG, "assert RESET");

    i2c_master_bus_handle_t bus = s_config.sccb_bus;
    if (s_config.acquire) {
        s_resources = true;
        ESP_RETURN_ON_ERROR(s_config.acquire(s_config.context, &bus), TAG, "acquire camera resources");
    }
    ESP_RETURN_ON_FALSE(bus || s_config.sccb_scl != GPIO_NUM_NC, ESP_ERR_INVALID_STATE, TAG, "missing SCCB bus");
    ESP_RETURN_ON_ERROR(start_clock(), TAG, "start sensor clock");
    vTaskDelay(pdMS_TO_TICKS(5) + 1);
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(gpio_set_level(s_config.pwdn, 0), TAG, "release sensor power-down");
    }
    vTaskDelay(pdMS_TO_TICKS(2) + 1);
    if (s_reset) {
        ESP_RETURN_ON_ERROR(gpio_set_level(s_config.reset, 1), TAG, "release sensor reset");
    }
    /* The OV5640 driver waits for its configured power-on delay before SCCB access. */
    esp_video_init_dvp_config_t dvp = {
        .sccb_config = {.init_sccb = bus == NULL, .freq = s_config.sccb_hz},
        .reset_pin = GPIO_NUM_NC, .pwdn_pin = GPIO_NUM_NC,
        .dvp_pin = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .vsync_io = s_config.vsync, .de_io = s_config.hsync,
            .pclk_io = s_config.pclk, .xclk_io = GPIO_NUM_NC,
        },
    };
    memcpy(dvp.dvp_pin.data_io, s_config.data, sizeof(s_config.data));
    if (bus) {
        dvp.sccb_config.i2c_handle = bus;
    } else {
        dvp.sccb_config.i2c_config.port = s_config.sccb_port;
        dvp.sccb_config.i2c_config.scl_pin = s_config.sccb_scl;
        dvp.sccb_config.i2c_config.sda_pin = s_config.sccb_sda;
    }
    const esp_video_init_config_t config = {.dvp = &dvp};
    ESP_RETURN_ON_ERROR(esp_video_init_with_flags(&config, ESP_VIDEO_INIT_FLAGS_DVP), TAG, "init DVP video");
    s_video = true;
    const int fd = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_FALSE(close(fd) == 0, ESP_FAIL, TAG, "close DVP probe");
    s_ready = true;
    return ESP_OK;
}

static esp_err_t release_control_pin(gpio_num_t pin)
{
    ESP_RETURN_ON_ERROR(gpio_reset_pin(pin), TAG, "release sensor GPIO");
    /* Avoid pulling up an unpowered sensor. */
    return gpio_pullup_dis(pin);
}

esp_err_t candis_ov5640_start_with_config(const candis_ov5640_config_t *config)
{
    if (s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!config || !valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    s_configured = true;
    esp_err_t err = start_camera();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_ov5640_stop());
    }
    return err;
}

const char *candis_ov5640_device(void)
{
    return s_ready ? ESP_VIDEO_DVP_DEVICE_NAME : NULL;
}

esp_err_t candis_ov5640_stop(void)
{
    s_ready = false;
    if (s_video) {
        ESP_RETURN_ON_ERROR(esp_video_deinit_with_flags(ESP_VIDEO_INIT_FLAGS_DVP), TAG, "release DVP video");
        s_video = false;
    }
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(gpio_set_level(s_config.pwdn, 1), TAG, "assert PWDN");
    }
    if (s_reset) {
        ESP_RETURN_ON_ERROR(gpio_set_level(s_config.reset, 0), TAG, "assert RESET");
    }
    if (s_clock_channel) {
        const ledc_channel_config_t output = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .channel = s_config.xclk_channel, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_stop(LEDC_LOW_SPEED_MODE, s_config.xclk_channel, 0), TAG, "stop XCLK");
        ESP_RETURN_ON_ERROR(ledc_channel_config(&output), TAG, "release XCLK channel and pin");
        s_clock_channel = false;
    }
    if (s_clock_timer) {
        const ledc_timer_config_t clock = {
            .speed_mode = LEDC_LOW_SPEED_MODE, .timer_num = s_config.xclk_timer, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_pause(LEDC_LOW_SPEED_MODE, s_config.xclk_timer), TAG, "pause XCLK timer");
        ESP_RETURN_ON_ERROR(ledc_timer_config(&clock), TAG, "release XCLK timer");
        s_clock_timer = false;
    }
    if (s_resources) {
        ESP_RETURN_ON_ERROR(s_config.release(s_config.context), TAG, "release camera resources");
        s_resources = false;
    }
    if (s_reset) {
        ESP_RETURN_ON_ERROR(release_control_pin(s_config.reset), TAG, "release RESET pin");
        s_reset = false;
    }
    if (s_pwdn) {
        ESP_RETURN_ON_ERROR(release_control_pin(s_config.pwdn), TAG, "release PWDN pin");
        s_pwdn = false;
    }
    s_configured = false;
    return ESP_OK;
}
