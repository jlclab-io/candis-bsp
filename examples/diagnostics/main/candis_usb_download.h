/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD and Candis contributors
 * SPDX-License-Identifier: Apache-2.0
 * Sequence timing based on ESP-Mosaico ESP-Iris; see the BSP README sources.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* CDC assertion booleans, not active-low GPIO levels. Both ClassicReset and
 * USBJTAGSerialReset hold one opposing phase before swapping it. A request is
 * latched by the swap but is only released when the host returns both lines to
 * idle, which distinguishes the complete esptool sequence from an arbitrary
 * terminal transition. */
typedef struct {
    unsigned last;
    unsigned armed;
    int64_t armed_at;
    bool pending;
    int64_t pending_at;
} candis_usb_download_sequence_t;

static inline bool candis_usb_download_sequence(
    candis_usb_download_sequence_t *state, bool dtr, bool rts, int64_t now)
{
    const unsigned line = (dtr ? 1U : 0U) | (rts ? 2U : 0U);
    if (line == state->last) {
        return false;
    }
    state->last = line;
    if (state->pending) {
        const bool complete =
            line == 0U && now >= state->pending_at &&
            now - state->pending_at <= 500000;
        state->armed = 0;
        state->pending = false;
        if (line == 0U) {
            return complete;
        }
    }
    if (line == 0U) {
        state->armed = 0;
        return false;
    }
    if (line == 3U) {
        return false;
    }
    const int64_t held = now - state->armed_at;
    if (state->armed != 0U && state->armed != line &&
        held >= 40000 && held <= 1500000) {
        state->pending = true;
        state->pending_at = now;
    }
    state->armed = line;
    state->armed_at = now;
    return false;
}
