/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "api_suites.h"
#include "api_test.h"
#include "bsp/candis.h"
#include "bsp/candis_camera.h"
#include "bsp/candis_power.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAMAPI "candis-camera"

typedef struct {
    i2c_master_bus_handle_t bus;
    candis_expansion_ldo_handle_t supplies[3];
    bool reset_claimed;
    bool pwdn_claimed;
} fixture_resources_t;

static fixture_resources_t s_fixture;

static esp_err_t release_supplies(void)
{
    for (size_t i = sizeof(s_fixture.supplies) / sizeof(s_fixture.supplies[0]); i > 0; --i) {
        if (s_fixture.supplies[i - 1]) {
            ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(s_fixture.supplies[i - 1]), "camera_test",
                                "release supply");
            s_fixture.supplies[i - 1] = CANDIS_EXPANSION_LDO_HANDLE_NONE;
        }
    }
    return ESP_OK;
}

static esp_err_t fixture_acquire(void *context, i2c_master_bus_handle_t *bus)
{
    fixture_resources_t *resources = context;
    if (!resources || !bus) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    return ESP_ERR_INVALID_STATE;
#endif
    if (uart_is_driver_installed(UART_NUM_0)) {
        return ESP_ERR_INVALID_STATE;
    }
    const gpio_config_t pwdn = {
        .pin_bit_mask = UINT64_C(1) << GPIO_NUM_40,
        .mode = GPIO_MODE_OUTPUT_OD,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_40, 1), "camera_test", "release PWDN");
    ESP_RETURN_ON_ERROR(gpio_config(&pwdn), "camera_test", "claim PWDN");
    resources->pwdn_claimed = true;
    const gpio_config_t reset = {
        .pin_bit_mask = UINT64_C(1) << GPIO_NUM_39,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_39, 0), "camera_test", "assert reset");
    ESP_RETURN_ON_ERROR(gpio_config(&reset), "camera_test", "claim reset");
    resources->reset_claimed = true;
    ESP_RETURN_ON_ERROR(candis_i2c_acquire(&resources->bus), "camera_test", "acquire SCCB");
    *bus = resources->bus;
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO1, 2800, 2,
                                                     &resources->supplies[0]),
                        "camera_test", "camera DOVDD");
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO4, 2800, 2,
                                                     &resources->supplies[1]),
                        "camera_test", "camera AVDD");
    return candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5,
                                        &resources->supplies[2]);
}

static esp_err_t fixture_prepare(void *context)
{
    fixture_resources_t *resources = context;
    if (!resources || !resources->pwdn_claimed) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_40, 0), "camera_test", "assert PWDN low");
    vTaskDelay(pdMS_TO_TICKS(6));
    return gpio_set_level(GPIO_NUM_39, 1);
}

static esp_err_t fixture_release(void *context)
{
    fixture_resources_t *resources = context;
    if (!resources) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_OK;
    if (resources->reset_claimed) {
        result = gpio_set_level(GPIO_NUM_39, 0);
    }
    if (resources->pwdn_claimed) {
        const esp_err_t pwdn = gpio_set_level(GPIO_NUM_40, 1);
        if (result == ESP_OK) {
            result = pwdn;
        }
    }
    const esp_err_t supplies = release_supplies();
    if (result == ESP_OK) {
        result = supplies;
    }
    if (resources->reset_claimed) {
        const esp_err_t reset = gpio_reset_pin(GPIO_NUM_39);
        if (result == ESP_OK) {
            result = reset;
        }
        resources->reset_claimed = false;
    }
    if (resources->pwdn_claimed) {
        const esp_err_t pwdn = gpio_reset_pin(GPIO_NUM_40);
        if (result == ESP_OK) {
            result = pwdn;
        }
        resources->pwdn_claimed = false;
    }
    if (resources->bus) {
        const esp_err_t bus = candis_i2c_release(resources->bus);
        if (result == ESP_OK) {
            result = bus;
        }
        if (bus == ESP_OK) {
            resources->bus = NULL;
        }
    }
    return result;
}

static candis_camera_config_t fixture_config(void)
{
    const candis_camera_profile_t *profile = candis_camera_profile_get(CANDIS_CAMERA_SENSOR_OV5640);
    candis_camera_config_t config = {
        .data = {GPIO_NUM_46, GPIO_NUM_47, GPIO_NUM_48, GPIO_NUM_49,
                 GPIO_NUM_50, GPIO_NUM_51, GPIO_NUM_52, GPIO_NUM_53},
        .pclk = GPIO_NUM_54,
        .vsync = GPIO_NUM_56,
        .hsync = GPIO_NUM_57,
        .xclk = GPIO_NUM_55,
        .xclk_hz = profile ? profile->xclk_hz : 24000000,
        .xclk_timer = LEDC_TIMER_1,
        .xclk_channel = LEDC_CHANNEL_1,
        .sccb_bus = NULL,
        .sccb_port = I2C_NUM_0,
        .sccb_scl = GPIO_NUM_NC,
        .sccb_sda = GPIO_NUM_NC,
        .sccb_hz = 100000,
        .acquire = fixture_acquire,
        .release = fixture_release,
        .context = &s_fixture,
        .validate_sensor = true,
        .expected_sensor = CANDIS_CAMERA_SENSOR_OV5640,
        .prepare = fixture_prepare,
    };
    return config;
}

static void test_profiles(void)
{
    const size_t count = candis_camera_profile_count();
    api_test_check_bool(CAMAPI, "candis_camera_profile_count", API_ASPECT_NORMAL,
                        count == CANDIS_CAMERA_SENSOR_COUNT, "one profile per sensor enum");
    bool enabled = false;
    bool names_ok = true;
    for (size_t i = 0; i < count; ++i) {
        const candis_camera_profile_t *profile = candis_camera_profile_at(i);
        names_ok = names_ok && profile && profile->model && profile->official_sensor_name;
        enabled = enabled || (profile && (profile->status_flags & CANDIS_CAMERA_PROFILE_BUILD_ENABLED));
        if (profile) {
            api_test_check_bool(CAMAPI, "candis_camera_profile_get", API_ASPECT_NORMAL,
                                candis_camera_profile_get(profile->sensor) == profile,
                                "profile lookup matches index");
            candis_camera_sensor_t parsed = CANDIS_CAMERA_SENSOR_UNKNOWN;
            const esp_err_t err = candis_camera_sensor_from_name(profile->model, &parsed);
            api_test_check_bool(CAMAPI, "candis_camera_sensor_from_name", API_ASPECT_NORMAL,
                                err == ESP_OK && parsed == profile->sensor,
                                "model name round trip");
            api_test_check_bool(CAMAPI, "candis_camera_sensor_name", API_ASPECT_NORMAL,
                                strcmp(candis_camera_sensor_name(profile->sensor), profile->model) == 0,
                                "sensor enum maps back to model");
        }
    }
    api_test_check_bool(CAMAPI, "candis_camera_profile_at", API_ASPECT_NORMAL,
                        names_ok, "all profile metadata populated");
    api_test_check_bool(CAMAPI, "candis_camera_profile_at", API_ASPECT_BOUNDARY,
                        candis_camera_profile_at(count) == NULL, "index at end returns NULL");
    api_test_check_bool(CAMAPI, "candis_camera_profile_get", API_ASPECT_BOUNDARY,
                        candis_camera_profile_get(CANDIS_CAMERA_SENSOR_UNKNOWN) == NULL,
                        "unknown enum returns NULL");
    api_test_check_bool(CAMAPI, "candis_camera_sensor_name", API_ASPECT_BOUNDARY,
                        candis_camera_sensor_name(CANDIS_CAMERA_SENSOR_UNKNOWN) == NULL,
                        "unknown enum has no name");
    api_test_check_bool(CAMAPI, "candis_camera_profile_count", API_ASPECT_NORMAL,
                        enabled, "at least one DVP detector is compiled");
    candis_camera_sensor_t sensor = CANDIS_CAMERA_SENSOR_OV5640;
    api_test_require(CAMAPI, "candis_camera_sensor_from_name", API_ASPECT_BOUNDARY,
                     candis_camera_sensor_from_name(NULL, &sensor), ESP_ERR_INVALID_ARG,
                     "NULL name");
    api_test_require(CAMAPI, "candis_camera_sensor_from_name", API_ASPECT_BOUNDARY,
                     candis_camera_sensor_from_name("OV5640", NULL), ESP_ERR_INVALID_ARG,
                     "NULL output");
    api_test_require(CAMAPI, "candis_camera_sensor_from_name", API_ASPECT_ERROR,
                     candis_camera_sensor_from_name("not-a-sensor", &sensor), ESP_ERR_NOT_FOUND,
                     "unknown model");
    api_test_check_bool(CAMAPI, "candis_camera_sensor_from_name", API_ASPECT_ERROR,
                        sensor == CANDIS_CAMERA_SENSOR_UNKNOWN, "unknown result stored");

    const candis_camera_profile_t *ov5640 = candis_camera_profile_get(CANDIS_CAMERA_SENSOR_OV5640);
    esp_cam_sensor_id_t id = {.pid = ov5640 ? ov5640->chip_ids[0] : 0};
    api_test_check_bool(CAMAPI, "candis_camera_profile_matches_id", API_ASPECT_NORMAL,
                        candis_camera_profile_matches_id(ov5640, &id), "known chip ID matches");
    id.pid ^= 0xffff;
    api_test_check_bool(CAMAPI, "candis_camera_profile_matches_id", API_ASPECT_ERROR,
                        !candis_camera_profile_matches_id(ov5640, &id), "wrong chip ID rejected");
    api_test_check_bool(CAMAPI, "candis_camera_profile_matches_id", API_ASPECT_BOUNDARY,
                        !candis_camera_profile_matches_id(NULL, NULL), "NULL args return false");
    api_test_check_bool(CAMAPI, "candis_camera_profile_supports_control", API_ASPECT_NORMAL,
                        candis_camera_profile_supports_control(
                            ov5640, CANDIS_CAMERA_CONTROL_HFLIP),
                        "OV5640 advertises horizontal flip");
    api_test_check_bool(CAMAPI, "candis_camera_profile_supports_control", API_ASPECT_BOUNDARY,
                        !candis_camera_profile_supports_control(
                            ov5640, CANDIS_CAMERA_CONTROL_COUNT),
                        "out-of-range control unsupported");
}

static void test_before_init(void)
{
    const candis_camera_profile_t *profile = NULL;
    esp_cam_sensor_format_t mode = {0};
    struct v4l2_fmtdesc fmt = {0};
    candis_camera_frame_t frame = {0};
    candis_camera_control_info_t info = {0};
    int32_t value = 0;
    size_t size = 0;
    uint8_t byte = 0;

    api_test_check_bool(CAMAPI, "candis_camera_device", API_ASPECT_ERROR,
                        candis_camera_device() == NULL, "no device before init");
    api_test_require(CAMAPI, "candis_camera_init", API_ASPECT_BOUNDARY,
                     candis_camera_init(NULL), ESP_ERR_INVALID_ARG, "NULL configuration");
    api_test_require(CAMAPI, "candis_camera_get_active_profile", API_ASPECT_BOUNDARY,
                     candis_camera_get_active_profile(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    api_test_require(CAMAPI, "candis_camera_get_active_profile", API_ASPECT_ERROR,
                     candis_camera_get_active_profile(&profile), ESP_ERR_INVALID_STATE,
                     "not initialized");
    api_test_require(CAMAPI, "candis_camera_read_sensor_id", API_ASPECT_BOUNDARY,
                     candis_camera_read_sensor_id(NULL), ESP_ERR_INVALID_ARG, "NULL sensor ID");
    api_test_require(CAMAPI, "candis_camera_get_mode", API_ASPECT_BOUNDARY,
                     candis_camera_get_mode(0, NULL), ESP_ERR_INVALID_ARG, "NULL mode output");
    api_test_require(CAMAPI, "candis_camera_get_mode", API_ASPECT_ERROR,
                     candis_camera_get_mode(0, &mode), ESP_ERR_INVALID_STATE, "no device");
    api_test_require(CAMAPI, "candis_camera_get_pixel_format", API_ASPECT_BOUNDARY,
                     candis_camera_get_pixel_format(0, NULL), ESP_ERR_INVALID_ARG,
                     "NULL format output");
    api_test_require(CAMAPI, "candis_camera_get_pixel_format", API_ASPECT_ERROR,
                     candis_camera_get_pixel_format(0, &fmt), ESP_ERR_INVALID_STATE, "no device");
    api_test_require(CAMAPI, "candis_camera_stream_start", API_ASPECT_ERROR,
                     candis_camera_stream_start(NULL, NULL), ESP_ERR_INVALID_STATE, "no device");
    api_test_require(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_BOUNDARY,
                     candis_camera_frame_acquire(NULL), ESP_ERR_INVALID_ARG, "NULL frame");
    api_test_require(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_ERROR,
                     candis_camera_frame_acquire(&frame), ESP_ERR_INVALID_STATE, "not streaming");
    api_test_require(CAMAPI, "candis_camera_frame_release", API_ASPECT_ERROR,
                     candis_camera_frame_release(), ESP_ERR_INVALID_STATE, "nothing borrowed");
    api_test_require(CAMAPI, "candis_camera_capture_frame", API_ASPECT_BOUNDARY,
                     candis_camera_capture_frame(NULL, 0, NULL), ESP_ERR_INVALID_ARG,
                     "NULL storage");
    api_test_require(CAMAPI, "candis_camera_capture_frame", API_ASPECT_ERROR,
                     candis_camera_capture_frame(&byte, 1, &size), ESP_ERR_INVALID_STATE,
                     "not streaming");
    api_test_require(CAMAPI, "candis_camera_control_query", API_ASPECT_BOUNDARY,
                     candis_camera_control_query(CANDIS_CAMERA_CONTROL_GAIN, NULL),
                     ESP_ERR_INVALID_ARG, "NULL info");
    api_test_require(CAMAPI, "candis_camera_control_query", API_ASPECT_BOUNDARY,
                     candis_camera_control_query(CANDIS_CAMERA_CONTROL_COUNT, &info),
                     ESP_ERR_INVALID_ARG, "invalid control enum");
    api_test_require(CAMAPI, "candis_camera_control_get", API_ASPECT_BOUNDARY,
                     candis_camera_control_get(CANDIS_CAMERA_CONTROL_GAIN, NULL),
                     ESP_ERR_INVALID_ARG, "NULL value");
    api_test_require(CAMAPI, "candis_camera_control_get", API_ASPECT_ERROR,
                     candis_camera_control_get(CANDIS_CAMERA_CONTROL_GAIN, &value),
                     ESP_ERR_INVALID_STATE, "not streaming");
    api_test_require(CAMAPI, "candis_camera_control_set", API_ASPECT_BOUNDARY,
                     candis_camera_control_set(CANDIS_CAMERA_CONTROL_COUNT, 0),
                     ESP_ERR_INVALID_ARG, "invalid control enum");
    api_test_require(CAMAPI, "candis_camera_set_flip", API_ASPECT_ERROR,
                     candis_camera_set_flip(true, true), ESP_ERR_INVALID_STATE, "not streaming");
    api_test_require(CAMAPI, "candis_camera_set_test_pattern", API_ASPECT_ERROR,
                     candis_camera_set_test_pattern(true), ESP_ERR_INVALID_STATE, "not streaming");
    api_test_require(CAMAPI, "candis_camera_stream_stop", API_ASPECT_LIFECYCLE,
                     candis_camera_stream_stop(), ESP_OK, "stop with no stream");
    api_test_require(CAMAPI, "candis_camera_stop", API_ASPECT_LIFECYCLE,
                     candis_camera_stop(), ESP_OK, "stop before init");
}

static void test_active_camera(const candis_camera_config_t *config)
{
    const candis_camera_profile_t *profile = NULL;
    esp_cam_sensor_id_t id = {0};
    esp_cam_sensor_format_t mode = {0};
    struct v4l2_fmtdesc fmt = {0};
    struct v4l2_pix_format pix = {0};
    candis_camera_control_info_t info = {0};

    api_test_check_bool(CAMAPI, "candis_camera_device", API_ASPECT_NORMAL,
                        candis_camera_device() != NULL, "DVP device path available");
    api_test_require(CAMAPI, "candis_camera_get_active_profile", API_ASPECT_NORMAL,
                     candis_camera_get_active_profile(&profile), ESP_OK,
                     "match detected sensor profile");
    if (profile) {
        api_test_check_bool(CAMAPI, "candis_camera_get_active_profile", API_ASPECT_HARDWARE,
                            profile->sensor == config->expected_sensor,
                            "expected OV5640 profile detected");
    }
    api_test_require(CAMAPI, "candis_camera_read_sensor_id", API_ASPECT_NORMAL,
                     candis_camera_read_sensor_id(&id), ESP_OK, "read sensor chip ID");
    api_test_check_bool(CAMAPI, "candis_camera_read_sensor_id", API_ASPECT_HARDWARE,
                        id.pid != 0 && id.pid != 0xffff, "chip ID is not erased/bus fault");

    api_test_require(CAMAPI, "candis_camera_get_mode", API_ASPECT_NORMAL,
                     candis_camera_get_mode(0, &mode), ESP_OK, "enumerate first sensor mode");
    api_test_check_bool(CAMAPI, "candis_camera_get_mode", API_ASPECT_NORMAL,
                        mode.width > 0 && mode.height > 0 && mode.fps > 0, "mode fields populated");
    api_test_require(CAMAPI, "candis_camera_get_pixel_format", API_ASPECT_NORMAL,
                     candis_camera_get_pixel_format(0, &fmt), ESP_OK,
                     "enumerate first DVP pixel format");
    if (fmt.pixelformat == 0) {
        esp_err_t end = candis_camera_get_pixel_format(1, &fmt);
        api_test_require(CAMAPI, "candis_camera_get_pixel_format", API_ASPECT_BOUNDARY,
                         end, ESP_ERR_NOT_FOUND, "enumeration end returns NOT_FOUND");
    } else {
        api_test_check_bool(CAMAPI, "candis_camera_get_pixel_format", API_ASPECT_NORMAL,
                            true, "pixel format fourcc populated");
    }

    if (profile) {
        static const candis_camera_control_id_t controls[] = {
            CANDIS_CAMERA_CONTROL_HFLIP,
            CANDIS_CAMERA_CONTROL_VFLIP,
            CANDIS_CAMERA_CONTROL_TEST_PATTERN,
            CANDIS_CAMERA_CONTROL_GAIN,
            CANDIS_CAMERA_CONTROL_EXPOSURE,
        };
        for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
            const esp_err_t err = candis_camera_control_query(controls[i], &info);
            if (candis_camera_profile_supports_control(profile, controls[i])) {
                api_test_require(CAMAPI, "candis_camera_control_query", API_ASPECT_NORMAL,
                                 err, ESP_OK, "query advertised control");
            } else {
                api_test_require(CAMAPI, "candis_camera_control_query", API_ASPECT_ERROR,
                                 err, ESP_ERR_NOT_SUPPORTED, "unadvertised control rejected");
            }
        }
    }

    candis_camera_stream_config_t stream = {0};
    esp_err_t err = candis_camera_stream_start(&stream, &pix);
    api_test_require(CAMAPI, "candis_camera_stream_start", API_ASPECT_NORMAL,
                     err, ESP_OK, "open DVP and start two-buffer capture");
    if (err != ESP_OK) {
        API_BLOCKED(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_HARDWARE,
                    "DVP stream did not start");
        API_BLOCKED(CAMAPI, "candis_camera_capture_frame", API_ASPECT_HARDWARE,
                    "DVP stream did not start");
        return;
    }
    api_test_check_bool(CAMAPI, "candis_camera_stream_start", API_ASPECT_NORMAL,
                        pix.width > 0 && pix.height > 0 && pix.pixelformat != 0,
                        "actual capture format populated");

    if (profile) {
        if (candis_camera_profile_supports_control(profile, CANDIS_CAMERA_CONTROL_HFLIP)) {
            int32_t original = 0;
            if (candis_camera_control_get(CANDIS_CAMERA_CONTROL_HFLIP, &original) == ESP_OK) {
                api_test_log(CAMAPI, "candis_camera_control_get", API_ASPECT_NORMAL, API_PASS,
                             "readable control returned value");
                const esp_err_t set = candis_camera_set_flip(true, !original);
                api_test_require(CAMAPI, "candis_camera_set_flip", API_ASPECT_NORMAL,
                                 set, ESP_OK, "toggle horizontal mirror");
                (void)candis_camera_set_flip(true, original);
            } else {
                API_BLOCKED(CAMAPI, "candis_camera_control_get", API_ASPECT_HARDWARE,
                            "control is write-only or not readable");
            }
        } else {
            api_test_require(CAMAPI, "candis_camera_control_get", API_ASPECT_ERROR,
                             candis_camera_control_get(CANDIS_CAMERA_CONTROL_HFLIP, &(int32_t){0}),
                             ESP_ERR_NOT_SUPPORTED, "unadvertised control read rejected");
        }
        if (candis_camera_profile_supports_control(profile, CANDIS_CAMERA_CONTROL_TEST_PATTERN)) {
            const esp_err_t on = candis_camera_set_test_pattern(true);
            api_test_require(CAMAPI, "candis_camera_set_test_pattern", API_ASPECT_NORMAL,
                             on, ESP_OK, "enable sensor test pattern");
            const esp_err_t off = candis_camera_set_test_pattern(false);
            api_test_require(CAMAPI, "candis_camera_set_test_pattern", API_ASPECT_LIFECYCLE,
                             off, ESP_OK, "disable sensor test pattern");
        } else {
            API_BLOCKED(CAMAPI, "candis_camera_set_test_pattern", API_ASPECT_HARDWARE,
                        "active sensor profile lacks test pattern");
        }
    }

    candis_camera_frame_t frame = {0};
    err = candis_camera_frame_acquire(&frame);
    api_test_require(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_NORMAL,
                     err, ESP_OK, "dequeue one DMA-backed frame");
    if (err == ESP_OK) {
        api_test_check_bool(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_HARDWARE,
                            frame.data && frame.size > 0, "valid frame payload");
        api_test_require(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_ERROR,
                         candis_camera_frame_acquire(&frame), ESP_ERR_INVALID_STATE,
                         "only one frame may be borrowed");
        api_test_require(CAMAPI, "candis_camera_frame_release", API_ASPECT_NORMAL,
                         candis_camera_frame_release(), ESP_OK, "return frame to V4L2");
        api_test_require(CAMAPI, "candis_camera_frame_release", API_ASPECT_LIFECYCLE,
                         candis_camera_frame_release(), ESP_ERR_INVALID_STATE,
                         "repeated release rejected");
    }

    uint8_t one_byte = 0;
    size_t captured = 0;
    err = candis_camera_capture_frame(&one_byte, 1, &captured);
    api_test_require(CAMAPI, "candis_camera_capture_frame", API_ASPECT_BOUNDARY,
                     err, ESP_ERR_INVALID_SIZE, "undersized caller buffer rejected");
    api_test_check_bool(CAMAPI, "candis_camera_capture_frame", API_ASPECT_BOUNDARY,
                        captured > 1, "required frame size reported");
    const size_t capacity = pix.sizeimage > captured ? pix.sizeimage : captured;
    uint8_t *storage = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (storage) {
        captured = 0;
        err = candis_camera_capture_frame(storage, capacity, &captured);
        api_test_require(CAMAPI, "candis_camera_capture_frame", API_ASPECT_NORMAL,
                         err, ESP_OK, "copy one frame into caller storage");
        if (err == ESP_OK) {
            bool nonzero = false;
            for (size_t i = 0; i < captured; ++i) {
                nonzero = nonzero || storage[i] != 0;
            }
            api_test_check_bool(CAMAPI, "candis_camera_capture_frame", API_ASPECT_HARDWARE,
                                captured > 0 && nonzero, "captured payload is non-empty");
        }
        heap_caps_free(storage);
    } else {
        API_BLOCKED(CAMAPI, "candis_camera_capture_frame", API_ASPECT_HARDWARE,
                    "cannot allocate frame capacity in PSRAM");
    }

    api_test_require(CAMAPI, "candis_camera_stream_stop", API_ASPECT_NORMAL,
                     candis_camera_stream_stop(), ESP_OK, "stop capture and release buffers");
    api_test_require(CAMAPI, "candis_camera_stream_stop", API_ASPECT_LIFECYCLE,
                     candis_camera_stream_stop(), ESP_OK, "repeated stream stop");
}

void api_tests_camera(void)
{
    api_test_suite_begin(CAMAPI);
    test_profiles();
    test_before_init();

    candis_camera_config_t config = fixture_config();
    api_test_check_bool(CAMAPI, "candis_camera_init", API_ASPECT_BOUNDARY,
                        config.acquire != NULL && config.release != NULL,
                        "resource callbacks installed");
    esp_err_t err = candis_camera_init(&config);
    if (err == ESP_OK) {
        api_test_log(CAMAPI, "candis_camera_init", API_ASPECT_NORMAL, API_PASS,
                     "DVP device and OV5640 detector initialized");
        api_test_require(CAMAPI, "candis_camera_init", API_ASPECT_LIFECYCLE,
                         candis_camera_init(&config), ESP_ERR_INVALID_STATE,
                         "second init rejected");
        test_active_camera(&config);
        esp_err_t stop = candis_camera_stop();
        api_test_require(CAMAPI, "candis_camera_stop", API_ASPECT_NORMAL,
                         stop, ESP_OK, "release DVP, XCLK and supplies");
        api_test_require(CAMAPI, "candis_camera_stop", API_ASPECT_LIFECYCLE,
                         candis_camera_stop(), ESP_OK, "repeated stop");
        err = candis_camera_init(&config);
        api_test_require(CAMAPI, "candis_camera_init", API_ASPECT_LIFECYCLE,
                         err, ESP_OK, "reinitialize after successful stop");
        if (err == ESP_OK) {
            api_test_require(CAMAPI, "candis_camera_stop", API_ASPECT_LIFECYCLE,
                             candis_camera_stop(), ESP_OK, "final stop");
        }
    } else {
        api_test_check_bool(CAMAPI, "candis_camera_init", API_ASPECT_ERROR,
                            err == ESP_ERR_NOT_FOUND || err == ESP_ERR_TIMEOUT ||
                            err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_SUPPORTED,
                            "camera detector unavailable");
        API_BLOCKED(CAMAPI, "candis_camera_init", API_ASPECT_HARDWARE,
                    "OV5640 fixture is not connected or did not respond");
        API_BLOCKED(CAMAPI, "candis_camera_stream_start", API_ASPECT_HARDWARE,
                    "camera not initialized");
        API_BLOCKED(CAMAPI, "candis_camera_frame_acquire", API_ASPECT_HARDWARE,
                    "camera not streaming");
        API_BLOCKED(CAMAPI, "candis_camera_capture_frame", API_ASPECT_HARDWARE,
                    "camera not streaming");
        (void)candis_camera_stop();
    }
    api_test_suite_end(CAMAPI);
}
