/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "bsp/candis_baseboard.h"
#include "bsp/candis_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Mount the BB card using GPIO39 and an existing, powered DC1SW domain. */
esp_err_t candis_bb_mount(const candis_baseboard_info_t *identity, const char *path, sdmmc_card_t **card);

#ifdef __cplusplus
}
#endif
