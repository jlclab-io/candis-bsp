/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_I2S_MCLK GPIO_NUM_15
#define BSP_I2S_SCLK GPIO_NUM_16
#define BSP_I2S_LCLK GPIO_NUM_17
#define BSP_I2S_DOUT GPIO_NUM_18
#define BSP_I2S_DSIN GPIO_NUM_19
#define BSP_POWER_AMP_IO GPIO_NUM_4

/** Prepare I2S and ES8389 using the existing ALDO2 supply. */
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);
/** Borrow the I2S interface from the current ESP-BSP audio instance. */
const audio_codec_data_if_t *bsp_audio_get_codec_itf(void);
/** Borrow the shared duplex codec. Open it once for simultaneous playback
 * and recording. Stop users and call bsp_audio_deinit() to release it. */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
esp_err_t bsp_audio_deinit(void);

#ifdef __cplusplus
}
#endif
