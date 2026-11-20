/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_audio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

#define FRAMES 240
#define CLIP_FRAMES (80 * FRAMES)
static const char *TAG = "candis_audio_test";

#ifdef CONFIG_CANDIS_DIAGNOSTICS_PLAYBACK
static esp_err_t play_test_tone(void)
{
    /* 1 kHz at 48 kHz. The triangle wave drives the tested limiter at its ceiling. */
    enum { TONE_FRAMES = 12000, PERIOD = 48, FADE = 240 };
    int16_t pcm[FRAMES * CANDIS_AUDIO_CHANNELS];
    size_t written;
    ESP_RETURN_ON_ERROR(candis_audio_set_output(true), TAG, "enable amplifiers");
    for (int first = 0; first < TONE_FRAMES; first += FRAMES) {
        for (int offset = 0; offset < FRAMES; ++offset) {
            int frame = first + offset;
            int phase = frame % PERIOD;
            int sample = (phase <= PERIOD / 2 ? phase : PERIOD - phase) * 640 - 7680;
            int fade = frame < FADE ? frame : TONE_FRAMES - 1 - frame;
            if (fade < FADE) {
                sample = sample * fade / FADE;
            }
            pcm[offset * 2] = pcm[offset * 2 + 1] = sample;
        }
        ESP_RETURN_ON_ERROR(candis_audio_playback_write(pcm, FRAMES, &written, 100), TAG, "send limited PCM");
        ESP_RETURN_ON_FALSE(written == FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short tone write");
    }
    vTaskDelay(pdMS_TO_TICKS(55));
    ESP_RETURN_ON_ERROR(candis_audio_set_volume(0), TAG, "mute both speakers");
    ESP_RETURN_ON_ERROR(candis_audio_playback_write(pcm, FRAMES, &written, 100), TAG, "send muted PCM");
    ESP_RETURN_ON_FALSE(written == FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short muted write");
    ESP_LOGI(TAG, "Limited playback=%d frames, muted TX=%u frames", TONE_FRAMES, (unsigned)written);
    return ESP_OK;
}
#endif

static esp_err_t transfer_pcm(int16_t *clip)
{
    int16_t pcm[FRAMES * CANDIS_AUDIO_CHANNELS];
    size_t frames = 0;
    int peak[2] = {0, 0};
    unsigned captured = 0;
    /* Discard the 400 ms startup transient before measuring both microphone slots. */
    for (int i = 0; i < 160; ++i) {
        ESP_RETURN_ON_ERROR(candis_audio_read(pcm, FRAMES, &frames, 100), TAG, "capture PCM");
        ESP_RETURN_ON_FALSE(frames == FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short PCM read");
        if (i < 80) {
            continue;
        }
        if (clip) {
            memcpy(clip + captured * CANDIS_AUDIO_CHANNELS, pcm, sizeof(pcm));
        }
        captured += frames;
        for (size_t j = 0; j < frames * CANDIS_AUDIO_CHANNELS; ++j) {
            int magnitude = pcm[j] < 0 ? -pcm[j] : pcm[j];
            if (magnitude > peak[j % 2]) {
                peak[j % 2] = magnitude;
            }
        }
    }
    const int16_t silence[FRAMES * CANDIS_AUDIO_CHANNELS] = {0};
    ESP_RETURN_ON_ERROR(candis_audio_write(silence, FRAMES, &frames, 100), TAG, "write silent PCM");
    ESP_RETURN_ON_FALSE(frames == FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short PCM write");
    ESP_LOGI(TAG, "PCM RX=%u frames, peak L=%d R=%d, silent TX=%u frames", captured, peak[0], peak[1], (unsigned)frames);
    return ESP_OK;
}

#ifdef CONFIG_CANDIS_DIAGNOSTICS_RECORD_REPLAY
static esp_err_t play_recording(int16_t *clip)
{
    for (size_t i = 0; i < FRAMES; ++i) {
        for (unsigned channel = 0; channel < CANDIS_AUDIO_CHANNELS; ++channel) {
            const size_t leading = i * CANDIS_AUDIO_CHANNELS + channel;
            const size_t trailing = (CLIP_FRAMES - 1 - i) * CANDIS_AUDIO_CHANNELS + channel;
            clip[leading] = (int)clip[leading] * (int)i / FRAMES;
            clip[trailing] = (int)clip[trailing] * (int)i / FRAMES;
        }
    }
    ESP_RETURN_ON_ERROR(candis_audio_set_volume(20), TAG, "set replay volume");
    ESP_RETURN_ON_ERROR(candis_audio_set_output(true), TAG, "enable playback");
    for (size_t offset = 0; offset < CLIP_FRAMES; offset += FRAMES) {
        size_t written;
        ESP_RETURN_ON_ERROR(candis_audio_playback_write(clip + offset * CANDIS_AUDIO_CHANNELS,
                             FRAMES, &written, 100), TAG, "replay PCM");
        ESP_RETURN_ON_FALSE(written == FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short clip write");
    }
    vTaskDelay(pdMS_TO_TICKS(55));
    ESP_RETURN_ON_ERROR(candis_audio_set_volume(0), TAG, "mute replay");
    ESP_LOGI(TAG, "Recorded and replayed %u stereo frames", (unsigned)CLIP_FRAMES);
    return ESP_OK;
}
#endif

esp_err_t candis_diagnostics_audio(void)
{
    for (unsigned cycle = 1; cycle <= 2; ++cycle) {
        ESP_RETURN_ON_ERROR(candis_audio_open(true), TAG, "open audio");
        ESP_LOGI(TAG, "ES8389 default gain verified: PGA 30.5 dB, DAC 0xB3/0xB3, amplifier off");
        int16_t *clip = NULL;
        esp_err_t err = ESP_OK;
#ifdef CONFIG_CANDIS_DIAGNOSTICS_RECORD_REPLAY
        if (cycle == 1) {
            clip = malloc(CLIP_FRAMES * CANDIS_AUDIO_CHANNELS * sizeof(*clip));
            if (!clip) {
                err = ESP_ERR_NO_MEM;
            }
        }
#endif
        if (err == ESP_OK) {
            err = transfer_pcm(clip);
        }
#ifdef CONFIG_CANDIS_DIAGNOSTICS_RECORD_REPLAY
        if (err == ESP_OK && clip) {
            err = play_recording(clip);
        }
#endif
#ifdef CONFIG_CANDIS_DIAGNOSTICS_PLAYBACK
        if (err == ESP_OK && cycle == 1 && !clip) {
            err = play_test_tone();
        }
#endif
        free(clip);
        esp_err_t close = candis_audio_close();
        ESP_LOGI(TAG, "cycle %u PCM=%s close=%s", cycle, esp_err_to_name(err), esp_err_to_name(close));
        if (err != ESP_OK || close != ESP_OK) {
            return err == ESP_OK ? close : err;
        }
    }
    return ESP_OK;
}
