/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool auto_exposure;
    bool auto_gain;
    bool auto_white_balance;
    uint16_t exposure_lines; /**< Whole lines, from 1 to frame length minus one in manual mode. */
    uint16_t gain_q4; /**< Manual analog gain, 16 is 1x and 1023 is 63.9375x. */
    uint16_t red_gain_q10; /**< Manual ISP gain, 1024 is 1x. */
    uint16_t green_gain_q10;
    uint16_t blue_gain_q10;
} candis_ov5640_controls_t;

/** Read controls and optionally the frame length in lines.
 * Automatic values can change while individual SCCB reads are in progress.
 * Use the capture task between borrowed frames.
 */
esp_err_t candis_ov5640_get_controls(candis_ov5640_controls_t *controls, uint16_t *frame_lines);

/** Schedule a group update for the next frame boundary. Auto modes ignore their manual values.
 * Manual white-balance gains range from 1 to 4095. If control transfer fails, the caller may
 * read back state and retry. Wait for the pending update before submitting another.
 * The control task owns OV5640 group 0 during capture.
 */
esp_err_t candis_ov5640_set_controls(const candis_ov5640_controls_t *controls);

typedef enum {
    CANDIS_OV5640_WB_AUTO,
    CANDIS_OV5640_WB_SUNNY,
    CANDIS_OV5640_WB_CLOUDY,
    CANDIS_OV5640_WB_OFFICE,
    CANDIS_OV5640_WB_HOME,
} candis_ov5640_wb_preset_t;

/** Set an automatic-exposure target level from -5 (dark) to +5 (bright). */
esp_err_t candis_ov5640_set_ae_level(int8_t level);

/** Select automatic white balance or a calibrated built-in lighting preset. */
esp_err_t candis_ov5640_set_white_balance_preset(candis_ov5640_wb_preset_t preset);

typedef struct {
    bool luma_enabled;
    bool saturation_enabled;
    bool manual_saturation;
    int16_t brightness; /**< Signed luma adjustment, -255 to +255. */
    uint8_t contrast; /**< OV5640 luma gain code, 0x20 is the default. */
    uint8_t saturation_u; /**< U gain in manual mode, upper UV adjustment bound in automatic mode. */
    uint8_t saturation_v; /**< V gain in manual mode, lower UV adjustment bound in automatic mode. */
} candis_ov5640_isp_t;

/** Read brightness, contrast and saturation settings between borrowed frames. */
esp_err_t candis_ov5640_get_isp(candis_ov5640_isp_t *settings);

/** Schedule an ISP adjustment at a frame boundary. The DVP mode must enable the SDE block.
 * Fixed Y/U/V effects use the same registers and cannot be active during this adjustment.
 */
esp_err_t candis_ov5640_set_isp(const candis_ov5640_isp_t *settings);

typedef struct {
    bool auto_sharpness;
    int8_t sharpness; /**< Manual level, -3 to +3. Ignored when auto_sharpness is true. */
    uint8_t denoise; /**< 0 disables manual denoise. Levels 1 to 8 select manual strength. */
} candis_ov5640_cip_t;

/** Read the OV5640 CIP manual/automatic sharpness and denoise settings. */
esp_err_t candis_ov5640_get_cip(candis_ov5640_cip_t *settings);

/** Schedule bounded sharpness and denoise settings for the next frame group. */
esp_err_t candis_ov5640_set_cip(const candis_ov5640_cip_t *settings);

typedef enum {
    CANDIS_OV5640_EFFECT_NORMAL,
    CANDIS_OV5640_EFFECT_NEGATIVE,
    CANDIS_OV5640_EFFECT_GRAYSCALE,
    CANDIS_OV5640_EFFECT_RED_TINT,
    CANDIS_OV5640_EFFECT_GREEN_TINT,
    CANDIS_OV5640_EFFECT_BLUE_TINT,
    CANDIS_OV5640_EFFECT_SEPIA,
} candis_ov5640_effect_t;

/** Apply a built-in OV5640 image effect. */
esp_err_t candis_ov5640_set_color_effect(candis_ov5640_effect_t effect);

/** Set the sensor color-matrix saturation from -4 to +4 using Espressif's OV5640 mapping. */
esp_err_t candis_ov5640_set_cmx_saturation(int8_t level);

typedef struct {
    bool enabled;
    uint8_t points[16]; /**< Monotonically nondecreasing output codes for 16 input segments. */
} candis_ov5640_gamma_t;

/** Read the OV5640 gamma enable flag and 16 curve points. */
esp_err_t candis_ov5640_get_gamma(candis_ov5640_gamma_t *gamma);

/** Configure the OV5640 gamma table and enable flag at the next frame boundary. */
esp_err_t candis_ov5640_set_gamma(const candis_ov5640_gamma_t *gamma);
/** Load an Espressif reference curve (0: normal, 1: alternate) and enable gamma. */
esp_err_t candis_ov5640_set_gamma_preset(unsigned preset);

#ifdef __cplusplus
}
#endif
