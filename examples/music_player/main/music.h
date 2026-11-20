/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "bsp/candis_audio.h"

typedef struct {
    unsigned step;
    unsigned frame;
    uint32_t phase[2];
    uint32_t increment[2];
} music_cursor_t;

void music_init(void);
/* Render the next mono block. Returns true at the end of each loop. */
bool music_render(music_cursor_t *cursor, int16_t block[CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES]);
