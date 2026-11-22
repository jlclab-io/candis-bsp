/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_camera_profile_internal.h"

const candis_camera_profile_t candis_camera_profile_gc032a = {
    .sensor = CANDIS_CAMERA_SENSOR_GC032A,
    .model = "GC032A",
    .official_sensor_name = "GC032A",
    .default_kconfig = "CONFIG_CAMERA_GC032A_DVP_DEFAULT_FMT_RGB565_640X480_30FPS",
    .default_mode = "DVP_8bit_24Minput_RGB565_640x480_30fps",
    .sccb_addresses = {0x21, 0},
    .sccb_address_count = 1,
    .chip_ids = {0x232a, 0},
    .chip_id_count = 1,
    .xclk_hz = 24000000u,
    .width = 640,
    .height = 480,
    .fps = 30,
    .sensor_format = ESP_CAM_SENSOR_PIXFORMAT_RGB565_BE,
    .v4l2_pixel_format = V4L2_PIX_FMT_RGB565X,
    .format_flags = CANDIS_CAMERA_PROFILE_FORMAT_RGB565 |
                    CANDIS_CAMERA_PROFILE_FORMAT_YUV422,
#ifdef CONFIG_CAMERA_GC032A_AUTO_DETECT_DVP_INTERFACE_SENSOR
    .status_flags = CANDIS_CAMERA_PROFILE_BUILD_ENABLED,
#endif
    .control_flags = CANDIS_CAMERA_PROFILE_CONTROL_FLIP |
                     CANDIS_CAMERA_PROFILE_CONTROL_TEST_PATTERN,
    .notes = "Espressif names the default Kconfig choice RGB565. The format table declares big-endian RGB565.",
};
