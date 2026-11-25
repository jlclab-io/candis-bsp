/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/esp-bsp.h"
#include "bsp/candis_camera.h"
#include "bsp/candis_camera_profile.h"
#include "bsp/candis_dock.h"
#include "tg28_esp.h"

_Static_assert(BSP_CAPS_DISPLAY == 1, "Candis core display capability changed");
_Static_assert(BSP_CAPS_TOUCH == 1, "Candis core touch capability changed");
_Static_assert(BSP_CAPS_POWER == 1, "Candis core power capability changed");
_Static_assert(BSP_CAPS_EXPANSION == 1, "Candis core expansion capability changed");
_Static_assert(BSP_LCD_H_RES == 480 && BSP_LCD_V_RES == 480,
               "Candis native panel geometry changed");
_Static_assert(BSP_LCD_BITS_PER_PIXEL == 16, "Native panel must stay RGB565");
_Static_assert(BSP_LCD_COLOR_FORMAT == ESP_LCD_COLOR_FORMAT_RGB565,
               "Native panel format macro changed");
_Static_assert(BSP_LCD_BIGENDIAN == 0, "Native panel byte order changed");

_Static_assert(CANDIS_AUDIO_RATE == 48000, "Audio sample rate contract changed");
_Static_assert(CANDIS_AUDIO_CHANNELS == 2, "Audio must remain stereo");
_Static_assert(CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES == 240,
               "Playback block contract changed");
_Static_assert(CANDIS_MOTOR_MAX_PULSE_MS == 600, "Motor pulse safety limit changed");
_Static_assert(CANDIS_BASEBOARD_IMAGE_SIZE == 134, "Baseboard EEPROM image size changed");
_Static_assert(CANDIS_EXPANSION_LDO_HANDLE_NONE == (candis_expansion_ldo_handle_t)0,
               "Invalid LDO handle must remain zero");

_Static_assert(CANDIS_CAMERA_SENSOR_COUNT == 17,
               "Every supported DVP sensor must have one public enum slot");
_Static_assert(CANDIS_CAMERA_CONTROL_COUNT == 8,
               "Camera public control enum changed");

_Static_assert(TG28_ESP_DEFAULT_ADDRESS == 0x34, "TG28-ESP default address changed");
_Static_assert(TG28_ESP_CHIP_ID == 0x4a, "TG28-ESP chip ID changed");
_Static_assert(TG28_ESP_IRQ_ALL == 0xdfffffU, "TG28-ESP public IRQ mask changed");

/* Compile every public default initializer under both supported IDF versions. */
__attribute__((used)) static const candis_display_config_t s_display_default =
    CANDIS_DISPLAY_CONFIG_DEFAULT();
__attribute__((used)) static const candis_storage_config_t s_storage_default =
    CANDIS_STORAGE_CONFIG_DEFAULT();
__attribute__((used)) static const candis_motor_config_t s_motor_default =
    CANDIS_MOTOR_CONFIG_DEFAULT();
__attribute__((used)) static const candis_imu_config_t s_imu_default =
    CANDIS_IMU_CONFIG_DEFAULT();
__attribute__((used)) static const tg28_esp_config_t s_tg28_default =
    TG28_ESP_CONFIG_DEFAULT();

/* These checks reject accidental signature drift in representative public APIs. */
__attribute__((used)) static esp_err_t (*const s_audio_open)(bool) = candis_audio_open;
__attribute__((used)) static esp_err_t (*const s_audio_read)(int16_t *, size_t, size_t *,
                                                               uint32_t) = candis_audio_read;
__attribute__((used)) static esp_err_t (*const s_display_get_stats)(
    candis_display_stats_t *) = candis_display_get_stats;
__attribute__((used)) static esp_err_t (*const s_camera_init)(
    const candis_camera_config_t *) = candis_camera_init;
__attribute__((used)) static esp_err_t (*const s_camera_frame_acquire)(
    candis_camera_frame_t *) = candis_camera_frame_acquire;
__attribute__((used)) static esp_err_t (*const s_dock_open)(
    bool, candis_baseboard_info_t *) = candis_dock_open;
__attribute__((used)) static esp_err_t (*const s_tg28_open)(
    tg28_esp_t *, i2c_master_bus_handle_t, const tg28_esp_config_t *) = tg28_esp_open;
