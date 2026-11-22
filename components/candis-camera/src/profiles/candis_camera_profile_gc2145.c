/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_gc2145 = {
    .sensor = CANDIS_CAMERA_SENSOR_GC2145,
    .model = "GC2145",
    .official_sensor_name = "GC2145",
    .default_kconfig = "CONFIG_CAMERA_GC2145_DVP_DEFAULT_FMT_RGB565_BE_640X480_15FPS",
    .default_mode = "DVP_8bit_20Minput_RGB565_BE_640x480_15fps",
    .sccb_addresses = {0x3c, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x2145, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 640,
    .height = 480,
    .fps = 15,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_BE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565X,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 |
                    CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_GC2145_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN |
                     CANDIS_CAMERA_PROFILE_CONTROL_AE_LEVEL,
    .notes = "The upstream test pattern is intended for UXGA.",
};
