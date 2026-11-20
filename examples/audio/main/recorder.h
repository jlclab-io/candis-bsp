/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp/candis_audio.h"

#define RECORDER_SECONDS 3
#define RECORDER_MAX_FRAMES (CANDIS_AUDIO_RATE * RECORDER_SECONDS)
#define RECORDER_DEFAULT_VOLUME 50
#define RECORDER_MAX_VOLUME 100

typedef enum {
    RECORDER_IDLE,
    RECORDER_RECORD,
    RECORDER_PLAY,
} recorder_mode_t;

typedef enum {
    RECORDER_STEREO,
    RECORDER_LEFT_ONLY,
    RECORDER_RIGHT_ONLY,
} recorder_route_t;

typedef struct {
    uint32_t request;
    recorder_mode_t mode;
    size_t clip_frames;
    size_t progress;
    unsigned peak[CANDIS_AUDIO_CHANNELS];
    uint8_t clipped_channels;
    bool active;
    esp_err_t result;
    esp_err_t cleanup;
} recorder_status_t;

esp_err_t recorder_init(void);
void recorder_deinit(void);
/* Run on the app task; LVGL calls the non-blocking functions below. */
void recorder_run(void);
bool recorder_start(recorder_mode_t mode, recorder_route_t route, uint32_t request);
void recorder_set_volume(unsigned volume);
void recorder_stop(void);
bool recorder_status(recorder_status_t *status);
