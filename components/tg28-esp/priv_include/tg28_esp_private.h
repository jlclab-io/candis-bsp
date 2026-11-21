/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include "tg28_esp.h"

esp_err_t tg28_read_regs(const tg28_esp_t *pmu, uint8_t reg, uint8_t *data, size_t length);
esp_err_t tg28_write_reg(const tg28_esp_t *pmu, uint8_t reg, uint8_t value);
esp_err_t tg28_update_reg(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, uint8_t value);

esp_err_t tg28_read_flag(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, bool *enabled);
esp_err_t tg28_require_protection_opt_in(const tg28_esp_t *pmu);
esp_err_t tg28_update_protected(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, uint8_t value);
esp_err_t tg28_read_voltage_step(const tg28_esp_t *pmu, uint8_t reg, uint16_t min_mv, uint16_t *mv);
esp_err_t tg28_write_voltage_step(const tg28_esp_t *pmu, uint8_t reg, uint16_t min_mv, uint16_t mv);

static inline esp_err_t tg28_read_reg(const tg28_esp_t *pmu, uint8_t reg, uint8_t *value)
{
    return tg28_read_regs(pmu, reg, value, 1);
}
