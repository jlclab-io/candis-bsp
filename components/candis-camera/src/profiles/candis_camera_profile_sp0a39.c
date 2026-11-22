/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_sp0a39 = {
    .sensor = CANDIS_CAMERA_SENSOR_SP0A39,
    .model = "SP0A39",
    .official_sensor_name = "SP0A39",
    .default_kconfig = "CONFIG_CAMERA_SP0A39_DVP_DEFAULT_FMT_GRAY_640X480_30FPS",
    .default_mode = "DVP_8bit_24Minput_Gray_640x480_30fps",
    .sccb_addresses = {0x21, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x0a39, 0},
    .chip_id_count = 1,
    .xclk_hz = 24000000u,
    .width = 640,
    .height = 480,
    .fps = 30,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_GRAYSCALE,
    .v4l2_pixel_format = V4L2_PIX_FMT_GREY,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_GRAYSCALE,
#ifdef CONFIG_CAMERA_SP0A39_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
    .notes = "The upstream driver requires hardware PWDN for stable standby.",
};
