/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_ov5640 = {
    .sensor = CANDIS_CAMERA_SENSOR_OV5640,
    .model = "OV5640",
    .official_sensor_name = "OV5640",
    .default_kconfig = "CONFIG_CAMERA_OV5640_DVP_DEFAULT_FMT_RGB565_BE_800X600_10FPS",
    .default_mode = "DVP_8bit_24Minput_RGB565_BE_800x600_10fps",
    .sccb_addresses = {0x3c, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x5640, 0},
    .chip_id_count = 1,
    .xclk_hz = 24000000u,
    .width = 800,
    .height = 600,
    .fps = 10,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_BE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565X,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 | CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_OV5640_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
    .notes = "Use the reference mode's XCLK and the camera module's power-up timing.",
};
