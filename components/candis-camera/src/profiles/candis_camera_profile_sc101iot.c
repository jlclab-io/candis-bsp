/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_sc101iot = {
    .sensor = CANDIS_CAMERA_SENSOR_SC101IOT,
    .model = "SC101IOT",
    .official_sensor_name = "SC101IOT",
    .default_kconfig = "CONFIG_CAMERA_SC101IOT_DVP_DEFAULT_FMT_YUV422_1280X720_15FPS",
    .default_mode = "DVP_8bit_20Minput_YUV422_UYVY_1280x720_15fps",
    .sccb_addresses = {0x68, 0},
    .sccb_address_count = 1,
    .chip_ids = {0xda4a, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 1280,
    .height = 720,
    .fps = 15,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_YUV422_UYVY,
    .v4l2_pixel_format = V4L2_PIX_FMT_UYVY,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_SC101IOT_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
};
