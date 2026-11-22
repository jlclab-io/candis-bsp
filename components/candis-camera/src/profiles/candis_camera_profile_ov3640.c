/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_ov3640 = {
    .sensor = CANDIS_CAMERA_SENSOR_OV3640,
    .model = "OV3640",
    .official_sensor_name = "OV3640",
    .default_kconfig = "CONFIG_CAMERA_OV3640_DVP_DEFAULT_FMT_RGB565_LE_640X480_7FPS",
    .default_mode = "DVP_8bit_24Minput_RGB565_LE_640x480_7fps",
    .sccb_addresses = {0x3c, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x364c, 0x3641},
    .chip_id_count = 2,
    .xclk_hz = 24000000u,
    .width = 640,
    .height = 480,
    .fps = 7,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_LE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 | CANDIS_CAMERA_PROFILE_FORMAT_YUV422 | CANDIS_CAMERA_PROFILE_FORMAT_JPEG,
#ifdef CONFIG_CAMERA_OV3640_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL,
    .readable_control_flags = CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL,
};
