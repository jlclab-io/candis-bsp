/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_gc0308 = {
    .sensor = CANDIS_CAMERA_SENSOR_GC0308,
    .model = "GC0308",
    .official_sensor_name = "GC0308",
    .default_kconfig = "CONFIG_CAMERA_GC0308_DVP_DEFAULT_FMT_YUV422_640X480_16FPS",
    .default_mode = "DVP_8bit_20Minput_YUV422_UYVY_640x480_16fps",
    .sccb_addresses = {0x21, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x009b, 0},
    .chip_id_count = 1,
    .xclk_hz = 20000000u,
    .width = 640,
    .height = 480,
    .fps = 16,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_YUV422_UYVY,
    .v4l2_pixel_format = V4L2_PIX_FMT_UYVY,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 |
                    CANDIS_CAMERA_PROFILE_FORMAT_YUV422 |
                    CANDIS_CAMERA_PROFILE_FORMAT_GRAYSCALE,
#ifdef CONFIG_CAMERA_GC0308_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
};
