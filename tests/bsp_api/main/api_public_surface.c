/* SPDX-License-Identifier: Apache-2.0 */
/* Generated from tests/bsp_api/api_inventory.json. */
#include "bsp/esp-bsp.h"
#include "bsp/candis_camera.h"
#include "bsp/candis_dock.h"
#include "tg28_esp.h"

#ifndef BSP_BOARD_CANDIS
#error "Public macro BSP_BOARD_CANDIS is not defined"
#endif
#ifndef BSP_CAPS_AUDIO
#error "Public macro BSP_CAPS_AUDIO is not defined"
#endif
#ifndef BSP_CAPS_AUDIO_MIC
#error "Public macro BSP_CAPS_AUDIO_MIC is not defined"
#endif
#ifndef BSP_CAPS_AUDIO_SPEAKER
#error "Public macro BSP_CAPS_AUDIO_SPEAKER is not defined"
#endif
#ifndef BSP_CAPS_BAT
#error "Public macro BSP_CAPS_BAT is not defined"
#endif
#ifndef BSP_CAPS_BUTTONS
#error "Public macro BSP_CAPS_BUTTONS is not defined"
#endif
#ifndef BSP_CAPS_CAMERA
#error "Public macro BSP_CAPS_CAMERA is not defined"
#endif
#ifndef BSP_CAPS_DISPLAY
#error "Public macro BSP_CAPS_DISPLAY is not defined"
#endif
#ifndef BSP_CAPS_EXPANSION
#error "Public macro BSP_CAPS_EXPANSION is not defined"
#endif
#ifndef BSP_CAPS_HUMITURE
#error "Public macro BSP_CAPS_HUMITURE is not defined"
#endif
#ifndef BSP_CAPS_IMU
#error "Public macro BSP_CAPS_IMU is not defined"
#endif
#ifndef BSP_CAPS_KNOB
#error "Public macro BSP_CAPS_KNOB is not defined"
#endif
#ifndef BSP_CAPS_LED
#error "Public macro BSP_CAPS_LED is not defined"
#endif
#ifndef BSP_CAPS_MOTOR
#error "Public macro BSP_CAPS_MOTOR is not defined"
#endif
#ifndef BSP_CAPS_POWER
#error "Public macro BSP_CAPS_POWER is not defined"
#endif
#ifndef BSP_CAPS_SDCARD
#error "Public macro BSP_CAPS_SDCARD is not defined"
#endif
#ifndef BSP_CAPS_TOUCH
#error "Public macro BSP_CAPS_TOUCH is not defined"
#endif
#ifndef BSP_CAPS_USB
#error "Public macro BSP_CAPS_USB is not defined"
#endif
#ifndef BSP_CAPS_USB_DEVICE
#error "Public macro BSP_CAPS_USB_DEVICE is not defined"
#endif
#ifndef BSP_CAPS_USB_HOST
#error "Public macro BSP_CAPS_USB_HOST is not defined"
#endif
#ifndef BSP_CONFIG_NO_GRAPHIC_LIB
#error "Public macro BSP_CONFIG_NO_GRAPHIC_LIB is not defined"
#endif
#ifndef BSP_I2C_NUM
#error "Public macro BSP_I2C_NUM is not defined"
#endif
#ifndef BSP_I2C_SCL
#error "Public macro BSP_I2C_SCL is not defined"
#endif
#ifndef BSP_I2C_SDA
#error "Public macro BSP_I2C_SDA is not defined"
#endif
#ifndef BSP_I2S_DOUT
#error "Public macro BSP_I2S_DOUT is not defined"
#endif
#ifndef BSP_I2S_DSIN
#error "Public macro BSP_I2S_DSIN is not defined"
#endif
#ifndef BSP_I2S_LCLK
#error "Public macro BSP_I2S_LCLK is not defined"
#endif
#ifndef BSP_I2S_MCLK
#error "Public macro BSP_I2S_MCLK is not defined"
#endif
#ifndef BSP_I2S_SCLK
#error "Public macro BSP_I2S_SCLK is not defined"
#endif
#ifndef BSP_LCD_BIGENDIAN
#error "Public macro BSP_LCD_BIGENDIAN is not defined"
#endif
#ifndef BSP_LCD_BITS_PER_PIXEL
#error "Public macro BSP_LCD_BITS_PER_PIXEL is not defined"
#endif
#ifndef BSP_LCD_COLOR_FORMAT
#error "Public macro BSP_LCD_COLOR_FORMAT is not defined"
#endif
#ifndef BSP_LCD_COLOR_SPACE
#error "Public macro BSP_LCD_COLOR_SPACE is not defined"
#endif
#ifndef BSP_LCD_H_RES
#error "Public macro BSP_LCD_H_RES is not defined"
#endif
#ifndef BSP_LCD_V_RES
#error "Public macro BSP_LCD_V_RES is not defined"
#endif
#ifndef BSP_POWER_AMP_IO
#error "Public macro BSP_POWER_AMP_IO is not defined"
#endif
#ifndef BSP_SD_MOUNT_POINT
#error "Public macro BSP_SD_MOUNT_POINT is not defined"
#endif
#ifndef CANDIS_AUDIO_CHANNELS
#error "Public macro CANDIS_AUDIO_CHANNELS is not defined"
#endif
#ifndef CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES
#error "Public macro CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES is not defined"
#endif
#ifndef CANDIS_AUDIO_RATE
#error "Public macro CANDIS_AUDIO_RATE is not defined"
#endif
#ifndef CANDIS_BASEBOARD_IMAGE_SIZE
#error "Public macro CANDIS_BASEBOARD_IMAGE_SIZE is not defined"
#endif
#ifndef CANDIS_DEFAULT_CHARGE_CURRENT_MA
#error "Public macro CANDIS_DEFAULT_CHARGE_CURRENT_MA is not defined"
#endif
#ifndef CANDIS_DISPLAY_CONFIG_DEFAULT
#error "Public macro CANDIS_DISPLAY_CONFIG_DEFAULT is not defined"
#endif
#ifndef CANDIS_EXPANSION_LDO_HANDLE_NONE
#error "Public macro CANDIS_EXPANSION_LDO_HANDLE_NONE is not defined"
#endif
#ifndef CANDIS_IMU_CONFIG_DEFAULT
#error "Public macro CANDIS_IMU_CONFIG_DEFAULT is not defined"
#endif
#ifndef CANDIS_MOTOR_CONFIG_DEFAULT
#error "Public macro CANDIS_MOTOR_CONFIG_DEFAULT is not defined"
#endif
#ifndef CANDIS_MOTOR_MAX_PULSE_MS
#error "Public macro CANDIS_MOTOR_MAX_PULSE_MS is not defined"
#endif
#ifndef CANDIS_OTP_CHARGE_CURRENT_MA
#error "Public macro CANDIS_OTP_CHARGE_CURRENT_MA is not defined"
#endif
#ifndef CANDIS_STORAGE_CONFIG_DEFAULT
#error "Public macro CANDIS_STORAGE_CONFIG_DEFAULT is not defined"
#endif
#ifndef ESP_LCD_COLOR_FORMAT_RGB565
#error "Public macro ESP_LCD_COLOR_FORMAT_RGB565 is not defined"
#endif
#ifndef ESP_LCD_COLOR_FORMAT_RGB888
#error "Public macro ESP_LCD_COLOR_FORMAT_RGB888 is not defined"
#endif
#ifndef TG28_ESP_CHIP_ID
#error "Public macro TG28_ESP_CHIP_ID is not defined"
#endif
#ifndef TG28_ESP_CONFIG_DEFAULT
#error "Public macro TG28_ESP_CONFIG_DEFAULT is not defined"
#endif
#ifndef TG28_ESP_DEFAULT_ADDRESS
#error "Public macro TG28_ESP_DEFAULT_ADDRESS is not defined"
#endif
#ifndef TG28_ESP_IRQ_ALL
#error "Public macro TG28_ESP_IRQ_ALL is not defined"
#endif

_Static_assert(sizeof(bsp_display_cfg_t) > 0, "Public type bsp_display_cfg_t must be complete");
_Static_assert(sizeof(bsp_display_config_t) > 0, "Public type bsp_display_config_t must be complete");
_Static_assert(sizeof(bsp_lcd_handles_t) > 0, "Public type bsp_lcd_handles_t must be complete");
_Static_assert(sizeof(bsp_sdcard_cfg_t) > 0, "Public type bsp_sdcard_cfg_t must be complete");
_Static_assert(sizeof(bsp_touch_config_t) > 0, "Public type bsp_touch_config_t must be complete");
_Static_assert(sizeof(bsp_usb_host_power_mode_t) > 0, "Public type bsp_usb_host_power_mode_t must be complete");
_Static_assert(sizeof(candis_baseboard_info_t) > 0, "Public type candis_baseboard_info_t must be complete");
_Static_assert(sizeof(candis_baseboard_kind_t) > 0, "Public type candis_baseboard_kind_t must be complete");
_Static_assert(sizeof(candis_camera_config_t) > 0, "Public type candis_camera_config_t must be complete");
_Static_assert(sizeof(candis_camera_control_id_t) > 0, "Public type candis_camera_control_id_t must be complete");
_Static_assert(sizeof(candis_camera_control_info_t) > 0, "Public type candis_camera_control_info_t must be complete");
_Static_assert(sizeof(candis_camera_frame_t) > 0, "Public type candis_camera_frame_t must be complete");
_Static_assert(sizeof(candis_camera_profile_control_flags_t) > 0, "Public type candis_camera_profile_control_flags_t must be complete");
_Static_assert(sizeof(candis_camera_profile_format_flags_t) > 0, "Public type candis_camera_profile_format_flags_t must be complete");
_Static_assert(sizeof(candis_camera_profile_status_flags_t) > 0, "Public type candis_camera_profile_status_flags_t must be complete");
_Static_assert(sizeof(candis_camera_profile_t) > 0, "Public type candis_camera_profile_t must be complete");
_Static_assert(sizeof(candis_camera_sensor_t) > 0, "Public type candis_camera_sensor_t must be complete");
_Static_assert(sizeof(candis_camera_stream_config_t) > 0, "Public type candis_camera_stream_config_t must be complete");
_Static_assert(sizeof(candis_capability_state_t) > 0, "Public type candis_capability_state_t must be complete");
_Static_assert(sizeof(candis_display_config_t) > 0, "Public type candis_display_config_t must be complete");
_Static_assert(sizeof(candis_display_stats_t) > 0, "Public type candis_display_stats_t must be complete");
_Static_assert(sizeof(candis_dock_module_scan_t) > 0, "Public type candis_dock_module_scan_t must be complete");
_Static_assert(sizeof(candis_dock_speaker_channel_t) > 0, "Public type candis_dock_speaker_channel_t must be complete");
_Static_assert(sizeof(candis_expansion_ldo_handle_t) > 0, "Public type candis_expansion_ldo_handle_t must be complete");
_Static_assert(sizeof(candis_expansion_ldo_t) > 0, "Public type candis_expansion_ldo_t must be complete");
_Static_assert(sizeof(candis_imu_config_t) > 0, "Public type candis_imu_config_t must be complete");
_Static_assert(sizeof(candis_imu_sample_t) > 0, "Public type candis_imu_sample_t must be complete");
_Static_assert(sizeof(candis_motor_config_t) > 0, "Public type candis_motor_config_t must be complete");
_Static_assert(sizeof(candis_motor_state_t) > 0, "Public type candis_motor_state_t must be complete");
_Static_assert(sizeof(candis_power_state_t) > 0, "Public type candis_power_state_t must be complete");
_Static_assert(sizeof(candis_rtc_update_irq_t) > 0, "Public type candis_rtc_update_irq_t must be complete");
_Static_assert(sizeof(candis_storage_config_t) > 0, "Public type candis_storage_config_t must be complete");
_Static_assert(sizeof(candis_storage_mount_config_t) > 0, "Public type candis_storage_mount_config_t must be complete");
_Static_assert(sizeof(candis_supply_state_t) > 0, "Public type candis_supply_state_t must be complete");
_Static_assert(sizeof(candis_touch_config_t) > 0, "Public type candis_touch_config_t must be complete");
_Static_assert(sizeof(candis_touch_point_t) > 0, "Public type candis_touch_point_t must be complete");
_Static_assert(sizeof(tg28_esp_adc_channel_t) > 0, "Public type tg28_esp_adc_channel_t must be complete");
_Static_assert(sizeof(tg28_esp_battery_direction_t) > 0, "Public type tg28_esp_battery_direction_t must be complete");
_Static_assert(sizeof(tg28_esp_charge_state_t) > 0, "Public type tg28_esp_charge_state_t must be complete");
_Static_assert(sizeof(tg28_esp_charger_t) > 0, "Public type tg28_esp_charger_t must be complete");
_Static_assert(sizeof(tg28_esp_chgled_mode_t) > 0, "Public type tg28_esp_chgled_mode_t must be complete");
_Static_assert(sizeof(tg28_esp_chgled_output_t) > 0, "Public type tg28_esp_chgled_output_t must be complete");
_Static_assert(sizeof(tg28_esp_chgled_t) > 0, "Public type tg28_esp_chgled_t must be complete");
_Static_assert(sizeof(tg28_esp_config_t) > 0, "Public type tg28_esp_config_t must be complete");
_Static_assert(sizeof(tg28_esp_gauge_config_t) > 0, "Public type tg28_esp_gauge_config_t must be complete");
_Static_assert(sizeof(tg28_esp_irq_event_t) > 0, "Public type tg28_esp_irq_event_t must be complete");
_Static_assert(sizeof(tg28_esp_irq_t) > 0, "Public type tg28_esp_irq_t must be complete");
_Static_assert(sizeof(tg28_esp_jeita_t) > 0, "Public type tg28_esp_jeita_t must be complete");
_Static_assert(sizeof(tg28_esp_ldo_t) > 0, "Public type tg28_esp_ldo_t must be complete");
_Static_assert(sizeof(tg28_esp_output_t) > 0, "Public type tg28_esp_output_t must be complete");
_Static_assert(sizeof(tg28_esp_power_command_t) > 0, "Public type tg28_esp_power_command_t must be complete");
_Static_assert(sizeof(tg28_esp_power_control_t) > 0, "Public type tg28_esp_power_control_t must be complete");
_Static_assert(sizeof(tg28_esp_power_key_t) > 0, "Public type tg28_esp_power_key_t must be complete");
_Static_assert(sizeof(tg28_esp_pwrok_t) > 0, "Public type tg28_esp_pwrok_t must be complete");
_Static_assert(sizeof(tg28_esp_rail_t) > 0, "Public type tg28_esp_rail_t must be complete");
_Static_assert(sizeof(tg28_esp_safety_timer_t) > 0, "Public type tg28_esp_safety_timer_t must be complete");
_Static_assert(sizeof(tg28_esp_shutdown_control_t) > 0, "Public type tg28_esp_shutdown_control_t must be complete");
_Static_assert(sizeof(tg28_esp_sleep_t) > 0, "Public type tg28_esp_sleep_t must be complete");
_Static_assert(sizeof(tg28_esp_status_t) > 0, "Public type tg28_esp_status_t must be complete");
_Static_assert(sizeof(tg28_esp_t) > 0, "Public type tg28_esp_t must be complete");
_Static_assert(sizeof(tg28_esp_ts_bias_mode_t) > 0, "Public type tg28_esp_ts_bias_mode_t must be complete");
_Static_assert(sizeof(tg28_esp_ts_point_t) > 0, "Public type tg28_esp_ts_point_t must be complete");
_Static_assert(sizeof(tg28_esp_ts_t) > 0, "Public type tg28_esp_ts_t must be complete");
_Static_assert(sizeof(tg28_esp_ts_threshold_t) > 0, "Public type tg28_esp_ts_threshold_t must be complete");
_Static_assert(sizeof(tg28_esp_watchdog_action_t) > 0, "Public type tg28_esp_watchdog_action_t must be complete");
