/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CANDIS_GAMEPAD_BUTTON_COUNT 6

typedef struct {
    bool initialized;
    uint32_t stable;
    uint32_t raw;
    int64_t changed_at[CANDIS_GAMEPAD_BUTTON_COUNT];
    uint8_t phase;
    int8_t quarters;
} candis_gamepad_filter_t;

/* Phase encoding is A in bit 1 and B in bit 0. Positive order follows 00, 01, 11, 10. */
static inline int8_t candis_gamepad_filter_update(candis_gamepad_filter_t *state, uint32_t raw,
                                                 uint8_t phase, int64_t now, uint32_t debounce_us)
{
    if (!state->initialized) {
        state->stable = raw;
        state->raw = raw;
        state->phase = phase;
        state->initialized = true;
        return 0;
    }
    const uint32_t changed = state->raw ^ raw;
    const uint32_t pending = state->stable ^ raw;
    state->raw = raw;
    for (unsigned i = 0; i < CANDIS_GAMEPAD_BUTTON_COUNT; ++i) {
        const uint32_t bit = 1U << i;
        if (changed & bit) {
            state->changed_at[i] = now;
        }
        if ((pending & bit) && now - state->changed_at[i] >= debounce_us) {
            state->stable ^= bit;
        }
    }

    static const int8_t quadrant[] = {0, 1, 3, 2};
    const int delta = (quadrant[phase] - quadrant[state->phase] + 4) % 4;
    state->phase = phase;
    if (delta == 2) {
        state->quarters = 0;
        return 0;
    }
    state->quarters += delta == 3 ? -1 : delta;
    if (state->quarters == 4 || state->quarters == -4) {
        const int8_t step = state->quarters / 4;
        state->quarters = 0;
        return step;
    }
    return 0;
}
