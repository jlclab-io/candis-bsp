/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_bf3a03 = {
    .sensor = CANDIS_CAMERA_SENSOR_BF3A03,
    .model = "BF3A03",
    .official_sensor_name = "BF3A03",
    .default_kconfig = "CONFIG_CAMERA_BF3A03_DVP_DEFAULT_FMT_YUV422_YUYV_640X480_15FPS",
    .default_mode = "DVP_8bit_20Minput_YUV422_YUYV_640x480_15fps",
    .sccb_addresses = {0x6e, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x3a03, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 640,
    .height = 480,
    .fps = 15,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_YUV422_YUYV,
    .v4l2_pixel_format = V4L2_PIX_FMT_YUYV,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_BF3A03_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL,
    .notes = "Official driver also exposes a mono register variant. The default profile selects color YUYV.",
};
