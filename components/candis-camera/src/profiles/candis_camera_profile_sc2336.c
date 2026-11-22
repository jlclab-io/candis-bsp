/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_sc2336 = {
    .sensor = CANDIS_CAMERA_SENSOR_SC2336,
    .model = "SC2336",
    .official_sensor_name = "SC2336",
    .default_kconfig = "CONFIG_CAMERA_SC2336_DVP_DEFAULT_FMT_RAW10_1280X720_30FPS",
    .default_mode = "DVP_8bit_24Minput_RAW10_1280x720_30fps",
    .sccb_addresses = {0x30, 0},
    .sccb_address_count = 1,
    .chip_ids = {0xcb3a, 0},
    .chip_id_count = 1,
    .xclk_hz = 24000000u,
    .width = 1280,
    .height = 720,
    .fps = 30,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RAW10,
    .v4l2_pixel_format = V4L2_PIX_FMT_SBGGR10,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RAW10,
#ifdef CONFIG_CAMERA_SC2336_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_GAIN |
                     CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE |
                     CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE_100US,
    .readable_control_flags = CANDIS_CAMERA_PROFILE_CONTROL_GAIN |
                              CANDIS_CAMERA_PROFILE_CONTROL_EXPOSURE,
    .notes = "The DVP mode outputs RAW10 Bayer data.",
};
