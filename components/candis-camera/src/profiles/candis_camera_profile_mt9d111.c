/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_mt9d111 = {
    .sensor = CANDIS_CAMERA_SENSOR_MT9D111,
    .model = "MT9D111",
    .official_sensor_name = "MT9D111",
    .default_kconfig = "CONFIG_CAMERA_MT9D111_DVP_DEFAULT_FMT_RGB565_320X240_10FPS",
    .default_mode = "DVP_8bit_20Minput_RGB565_LE_320x240_10fps",
    .sccb_addresses = {0x48, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x1519, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 320,
    .height = 240,
    .fps = 10,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_LE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 |
                    CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_MT9D111_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
};
