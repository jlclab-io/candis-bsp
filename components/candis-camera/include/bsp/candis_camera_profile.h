/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_cam_sensor_types.h"
#include "esp_err.h"
#include "linux/videodev2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Official Espressif 8-bit DVP sensors supported by this component. */
typedef enum {
    CANDIS_CAMERA_SENSOR_BF20A6 = 0,
    CANDIS_CAMERA_SENSOR_BF3031,
    CANDIS_CAMERA_SENSOR_BF3045,
    CANDIS_CAMERA_SENSOR_BF3925,
    CANDIS_CAMERA_SENSOR_BF3A03,
    CANDIS_CAMERA_SENSOR_GC0308,
    CANDIS_CAMERA_SENSOR_GC032A,
    CANDIS_CAMERA_SENSOR_GC2145,
    CANDIS_CAMERA_SENSOR_MT9D111,
    CANDIS_CAMERA_SENSOR_OV2640,
    CANDIS_CAMERA_SENSOR_OV3640,
    CANDIS_CAMERA_SENSOR_OV3660,
    CANDIS_CAMERA_SENSOR_OV5640,
    CANDIS_CAMERA_SENSOR_SC030IOT,
    CANDIS_CAMERA_SENSOR_SC101IOT,
    CANDIS_CAMERA_SENSOR_SC2336,
    CANDIS_CAMERA_SENSOR_SP0A39,
    CANDIS_CAMERA_SENSOR_COUNT,
    CANDIS_CAMERA_SENSOR_UNKNOWN = -1,
} candis_camera_sensor_t;

typedef enum {
    CANDIS_CAMERA_PROFILE_FORMAT_RGB565 = 1u << 0,
    CANDIS_CAMERA_PROFILE_FORMAT_YUV422 = 1u << 1,
    CANDIS_CAMERA_PROFILE_FORMAT_GRAYSCALE = 1u << 2,
    CANDIS_CAMERA_PROFILE_FORMAT_JPEG = 1u << 3,
    CANDIS_CAMERA_PROFILE_FORMAT_RAW8 = 1u << 4,
    CANDIS_CAMERA_PROFILE_FORMAT_RAW10 = 1u << 5,
} candis_camera_profile_format_flags_t;

typedef enum {
    CANDIS_CAMERA_PROFILE_BUILD_ENABLED = 1u << 0,
} candis_camera_profile_status_flags_t;

typedef enum {
    CANDIS_CAMERA_PROFILE_CONTROL_FLIP = 1u << 0,
    CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL = 1u << 1,
    CANDIS_CAMERA_PROFILE_CONTROL_JPEG_QUALITY = 1u << 2,
    CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN = 1u << 3,
    CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE_100US = 1u << 4,
    CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE = 1u << 5,
    CANDIS_CAMERA_PROFILE_CONTROL_GAIN = 1u << 6,
} candis_camera_profile_control_flags_t;

typedef enum {
    CANDIS_CAMERA_CONTROL_GAIN = 0,
    CANDIS_CAMERA_CONTROL_EXPOSURE,
    CANDIS_CAMERA_CONTROL_HFLIP,
    CANDIS_CAMERA_CONTROL_VFLIP,
    CANDIS_CAMERA_CONTROL_AE_LEVEL,
    CANDIS_CAMERA_CONTROL_JPEG_QUALITY,
    CANDIS_CAMERA_CONTROL_TEST_PATTERN,
    CANDIS_CAMERA_CONTROL_EXPOSURE_100US, /**< Absolute exposure in 100 us units. */
    CANDIS_CAMERA_CONTROL_COUNT,
} candis_camera_control_id_t;

/**
 * Model-specific DVP configuration. The dimensions, pixel format and XCLK
 * describe the supplied example's default mode. For a different mode, use
 * the XCLK specified by that mode in Espressif's sensor format table.
 */
typedef struct {
    candis_camera_sensor_t sensor;
    const char *model;
    const char *official_sensor_name;
    const char *default_kconfig;
    const char *default_mode;
    uint8_t sccb_addresses[2]; /**< Supported 7-bit addresses. */
    uint8_t sccb_address_count;
    uint16_t chip_ids[2];
    uint8_t chip_id_count;
    uint32_t xclk_hz;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    esp_cam_sensor_output_format_t sensor_format;
    uint32_t v4l2_pixel_format;
    uint32_t format_flags;    /**< DVP format families available in the upstream driver. */
    uint32_t status_flags;    /**< Build status of the upstream DVP detection entry. */
    uint32_t control_flags;   /**< Controls that can be written through ESP Video. */
    uint32_t readable_control_flags;
    const char *notes;
} candis_camera_profile_t;

const candis_camera_profile_t *candis_camera_profile_get(candis_camera_sensor_t sensor);
const candis_camera_profile_t *candis_camera_profile_at(size_t index);
size_t candis_camera_profile_count(void);
esp_err_t candis_camera_sensor_from_name(const char *name, candis_camera_sensor_t *sensor);
const char *candis_camera_sensor_name(candis_camera_sensor_t sensor);

/** Check a detected sensor ID against a selected profile. */
bool candis_camera_profile_matches_id(const candis_camera_profile_t *profile,
                                      const esp_cam_sensor_id_t *id);
bool candis_camera_profile_supports_control(const candis_camera_profile_t *profile,
                                            candis_camera_control_id_t control);

#ifdef __cplusplus
}
#endif
