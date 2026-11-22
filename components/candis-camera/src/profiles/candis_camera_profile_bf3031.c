/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_bf3031 = {
    .sensor = CANDIS_CAMERA_SENSOR_BF3031,
    .model = "BF3031",
    .official_sensor_name = "BF3031",
    .default_kconfig = "CONFIG_CAMERA_BF3031_DVP_DEFAULT_FMT_YUV422_838X640_25FPS",
    .default_mode = "DVP_8bit_20Minput_YUV422_838x640_25fps",
    .sccb_addresses = {0x6e, 0x6d},
    .sccb_address_count = 2,
    .chip_ids = {0x3031, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 838,
    .height = 640,
    .fps = 25,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_YUV422_YUYV,
    .v4l2_pixel_format = V4L2_PIX_FMT_YUYV,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_BF3031_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
    .notes = "Select CAMERA_BF3031_SID_HIGH for a high SID strap (0x6d); a low strap uses 0x6e.",
};
