/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include "music.h"
#include "score.h"

enum {
    STEPS_PER_BAR = sizeof(melody[0]),
    SONG_STEPS = sizeof(melody),
    NOTE_FRAMES = CANDIS_AUDIO_RATE / 4,
    ATTACK_FRAMES = CANDIS_AUDIO_RATE / 100,
    RELEASE_FRAMES = CANDIS_AUDIO_RATE / 25,
    TABLE_SIZE = 256,
    SINE_SCALE = 8192,
};

_Static_assert(sizeof(accompaniment) == sizeof(melody), "Voice lengths must match");
_Static_assert(NOTE_FRAMES % CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES == 0, "Blocks must align to notes");

static int16_t s_sine[TABLE_SIZE];
static uint32_t s_increment[128];

void music_init(void)
{
    for (unsigned i = 0; i < TABLE_SIZE; ++i) {
        s_sine[i] = (int16_t)(sinf(6.283185307f * i / TABLE_SIZE) * SINE_SCALE);
    }
    for (int note = 1; note < 128; ++note) {
        const float hz = 440.0f * exp2f((note - 69) / 12.0f);
        s_increment[note] = (uint32_t)(hz * 4294967296.0f / CANDIS_AUDIO_RATE);
    }
}

static int voice(uint32_t phase)
{
    const unsigned fundamental = phase >> 24;
    const unsigned harmonic = (phase << 1) >> 24;
    return (3 * s_sine[fundamental] + s_sine[harmonic]) / 4;
}

bool music_render(music_cursor_t *cursor, int16_t block[CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES])
{
    if (!cursor->frame) {
        const unsigned bar = cursor->step / STEPS_PER_BAR;
        const unsigned beat = cursor->step % STEPS_PER_BAR;
        cursor->phase[0] = cursor->phase[1] = 0;
        cursor->increment[0] = s_increment[melody[bar][beat]];
        cursor->increment[1] = s_increment[accompaniment[bar][beat]];
    }
    for (unsigned i = 0; i < CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES; ++i) {
        const unsigned frame = cursor->frame + i;
        const unsigned remaining = NOTE_FRAMES - 1 - frame;
        unsigned envelope = 256 - 128 * frame / NOTE_FRAMES;
        if (frame < ATTACK_FRAMES) {
            envelope = envelope * frame / ATTACK_FRAMES;
        }
        if (remaining < RELEASE_FRAMES) {
            envelope = envelope * remaining / RELEASE_FRAMES;
        }
        const int mix = (voice(cursor->phase[0]) * 7000 + voice(cursor->phase[1]) * 3600) / SINE_SCALE;
        block[i] = mix * (int)envelope / 256;
        cursor->phase[0] += cursor->increment[0];
        cursor->phase[1] += cursor->increment[1];
    }
    cursor->frame += CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES;
    if (cursor->frame == NOTE_FRAMES) {
        cursor->frame = 0;
        cursor->step = (cursor->step + 1) % SONG_STEPS;
        return cursor->step == 0;
    }
    return false;
}
