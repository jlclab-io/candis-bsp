/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/** Runtime state for optional hardware attached outside the core board. */
typedef enum {
    CANDIS_CAPABILITY_UNSUPPORTED,
    CANDIS_CAPABILITY_NOT_PRESENT,
    CANDIS_CAPABILITY_PRESENT,
    CANDIS_CAPABILITY_INITIALIZED,
} candis_capability_state_t;
