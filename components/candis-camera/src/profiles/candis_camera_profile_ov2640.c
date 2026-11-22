/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_ov2640 = {
    .sensor = CANDIS_CAMERA_SENSOR_OV2640,
    .model = "OV2640",
    .official_sensor_name = "OV2640",
    .default_kconfig = "CONFIG_CAMERA_OV2640_DVP_DEFAULT_FMT_RGB565_640X480_6FPS",
    .default_mode = "DVP_8bit_20Minput_RGB565_LE_640x480_6fps",
    .sccb_addresses = {0x30, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x0026, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 640,
    .height = 480,
    .fps = 6,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_LE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 |
                    CANDIS_CAMERA_PROFILE_FORMAT_YUV422 |
                    CANDIS_CAMERA_PROFILE_FORMAT_JPEG |
                    CANDIS_CAMERA_PROFILE_FORMAT_RAW8,
#ifdef CONFIG_CAMERA_OV2640_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL |
                     CANDIS_CAMERA_PROFILE_CONTROL_JPEG_QUALITY,
    .readable_control_flags = CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL |
                              CANDIS_CAMERA_PROFILE_CONTROL_JPEG_QUALITY,
};
