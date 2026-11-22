/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_ov3660 = {
    .sensor = CANDIS_CAMERA_SENSOR_OV3660,
    .model = "OV3660",
    .official_sensor_name = "OV3660",
    .default_kconfig = "CONFIG_CAMERA_OV3660_DVP_DEFAULT_FMT_YUV422_YUYV_640X480_10FPS",
    .default_mode = "DVP_8bit_20Minput_YUV422_YUYV_640x480_10fps",
    .sccb_addresses = {0x3c, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x3660, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 640,
    .height = 480,
    .fps = 10,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_YUV422_YUYV,
    .v4l2_pixel_format = V4L2_PIX_FMT_YUYV,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 | CANDIS_CAMERA_PROFILE_FORMAT_YUV422 | CANDIS_CAMERA_PROFILE_FORMAT_JPEG,
#ifdef CONFIG_CAMERA_OV3660_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL |
                     CANDIS_CAMERA_PROFILE_CONTROL_JPEG_QUALITY,
    .readable_control_flags = CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL,
};
