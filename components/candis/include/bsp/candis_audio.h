/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANDIS_AUDIO_RATE 48000
#define CANDIS_AUDIO_CHANNELS 2
#define CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES 240

/** Open duplex stereo PCM, 48 kHz and 16 bits, at 50% volume with the amplifier off. */
esp_err_t candis_audio_open(bool manage_power);
/** Set playback level, 0–100, with a -6 dB codec ceiling. Zero disables the amplifier. */
esp_err_t candis_audio_set_volume(unsigned volume);
/** Enable or disable the amplifier. Write silence before enabling or disabling. */
esp_err_t candis_audio_set_output(bool on);
/** PCM calls report actual frames even if the supplied timeout expires. */
esp_err_t candis_audio_read(int16_t *pcm, size_t frames, size_t *received, uint32_t timeout_ms);
esp_err_t candis_audio_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms);
/** Write up to 240 frames with up to 4x digital gain and a 25000 peak limit.
 * Fade the source at clip boundaries in the app. Use one TX writer per session.
 */
esp_err_t candis_audio_playback_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms);
/** Quiesce PCM readers and writers before closing. Serialize control calls in the app. */
esp_err_t candis_audio_close(void);

#ifdef __cplusplus
}
#endif
