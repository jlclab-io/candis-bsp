/* SPDX-License-Identifier: Apache-2.0 */
#include "tg28_esp_private.h"

typedef struct {
    uint8_t enable_reg;
    uint8_t enable_bit;
    uint8_t voltage_reg;
    uint8_t voltage_mask;
    uint16_t minimum_mv;
    uint16_t maximum_mv;
    uint8_t step_mv;
} rail_info_t;

static const rail_info_t rails[] = {
    [TG28_ESP_RAIL_DCDC1]   = {0x80, 0x01, 0x82, 0x1f, 1500, 3400, 100},
    [TG28_ESP_RAIL_DCDC2]   = {0x80, 0x02, 0x83, 0x7f,  500, 1540, 10},
    [TG28_ESP_RAIL_DCDC3]   = {0x80, 0x04, 0x84, 0x7f,  500, 1540, 10},
    [TG28_ESP_RAIL_DCDC4]   = {0x80, 0x08, 0x85, 0x7f,  500, 1840, 10},
    [TG28_ESP_RAIL_ALDO1]   = {0x90, 0x01, 0x92, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_ALDO2]   = {0x90, 0x02, 0x93, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_ALDO3]   = {0x90, 0x04, 0x94, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_ALDO4]   = {0x90, 0x08, 0x95, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_BLDO1]   = {0x90, 0x10, 0x96, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_BLDO2]   = {0x90, 0x20, 0x97, 0x1f,  500, 3500, 100},
    [TG28_ESP_RAIL_CPUSLDO] = {0x90, 0x40, 0x98, 0x1f,  500, 1400, 50},
    [TG28_ESP_RAIL_DC1SW]   = {.enable_reg = 0x90, .enable_bit = 0x80},
    [TG28_ESP_RAIL_DC4SW]   = {.enable_reg = 0x91, .enable_bit = 0x01},
};

static const rail_info_t *get_rail(tg28_esp_rail_t rail)
{
    return (unsigned)rail < sizeof(rails) / sizeof(rails[0]) ? &rails[rail] : NULL;
}

esp_err_t tg28_esp_read_dcdc_outputs(const tg28_esp_t *pmu, uint8_t *enabled)
{
    if (!enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x80, &value);
    if (err == ESP_OK) {
        *enabled = value & 15;
    }
    return err;
}

esp_err_t tg28_esp_read_rail_enabled(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *enabled)
{
    const rail_info_t *info = get_rail(rail);
    if (!info || !enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, info->enable_reg, &value);
    if (err == ESP_OK) {
        *enabled = value & info->enable_bit;
    }
    return err;
}

esp_err_t tg28_esp_set_rail_enabled(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool enabled)
{
    const rail_info_t *info = get_rail(rail);
    return info ? tg28_update_reg(pmu, info->enable_reg, info->enable_bit,
                                 enabled ? info->enable_bit : 0) : ESP_ERR_INVALID_ARG;
}

esp_err_t tg28_esp_read_rail_voltage(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint16_t *mv)
{
    if (rail == TG28_ESP_RAIL_DC1SW) {
        rail = TG28_ESP_RAIL_DCDC1;
    } else if (rail == TG28_ESP_RAIL_DC4SW) {
        rail = TG28_ESP_RAIL_DCDC4;
    }
    const rail_info_t *info = get_rail(rail);
    if (!info || !mv) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, info->voltage_reg, &value);
    if (err != ESP_OK) {
        return err;
    }
    const unsigned code = value & info->voltage_mask;
    const unsigned voltage = info->step_mv == 10 && code > 70 ?
                             1200 + (code - 70) * 20 : info->minimum_mv + code * info->step_mv;
    if (voltage > info->maximum_mv) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *mv = voltage;
    return ESP_OK;
}

esp_err_t tg28_esp_set_rail_voltage(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint16_t mv)
{
    const rail_info_t *info = get_rail(rail);
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!info->voltage_reg) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (mv < info->minimum_mv || mv > info->maximum_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool upper_range = info->step_mv == 10 && mv > 1200;
    const unsigned offset = mv - (upper_range ? 1200 : info->minimum_mv);
    const unsigned step = upper_range ? 20 : info->step_mv;
    if (offset % step) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, info->voltage_reg, info->voltage_mask, offset / step + (upper_range ? 70 : 0));
}

esp_err_t tg28_esp_read_dcdc_pwm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *forced)
{
    if (!forced || (unsigned)rail > TG28_ESP_RAIL_DCDC4) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x81, &value);
    if (err == ESP_OK) {
        *forced = value & (1U << (rail + 2));
    }
    return err;
}

esp_err_t tg28_esp_set_dcdc_pwm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool forced)
{
    if ((unsigned)rail > TG28_ESP_RAIL_DCDC4) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t bit = 1U << (rail + 2);
    return tg28_update_reg(pmu, 0x81, bit, forced ? bit : 0);
}

/* TG28-ESP Datasheet V1.0 sections 6.13.2.69–70, 72–73. */
esp_err_t tg28_esp_read_dvm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *enabled)
{
    if (rail != TG28_ESP_RAIL_DCDC2 && rail != TG28_ESP_RAIL_DCDC3) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_read_flag(pmu, rail == TG28_ESP_RAIL_DCDC2 ? 0x83 : 0x84, 0x80, enabled);
}

esp_err_t tg28_esp_set_dvm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool enabled)
{
    if (rail != TG28_ESP_RAIL_DCDC2 && rail != TG28_ESP_RAIL_DCDC3) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, rail == TG28_ESP_RAIL_DCDC2 ? 0x83 : 0x84, 0x80, enabled ? 0x80 : 0);
}

esp_err_t tg28_esp_read_dcdc_control(const tg28_esp_t *pmu, bool *ccm, bool *slow_ramp)
{
    if (!ccm || !slow_ramp) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x80, &value);
    if (err == ESP_OK) {
        *ccm = value & 0x40;
        *slow_ramp = value & 0x20;
    }
    return err;
}

esp_err_t tg28_esp_set_dcdc_control(const tg28_esp_t *pmu, bool ccm, bool slow_ramp)
{
    return tg28_update_protected(pmu, 0x80, 0x60, (ccm << 6) | (slow_ramp << 5));
}

esp_err_t tg28_esp_read_dcdc_spread(const tg28_esp_t *pmu, bool *enabled, uint8_t *khz)
{
    if (!enabled || !khz) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x81, &value);
    if (err == ESP_OK) {
        *enabled = value & 0x80;
        *khz = (value & 0x40) ? 100 : 50;
    }
    return err;
}

esp_err_t tg28_esp_set_dcdc_spread(const tg28_esp_t *pmu, bool enabled, uint8_t khz)
{
    if (khz != 50 && khz != 100) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x81, 0xc0, (enabled << 7) | ((khz == 100) << 6));
}

esp_err_t tg28_esp_read_dcdc_uvp_delay(const tg28_esp_t *pmu, uint16_t *us)
{
    if (!us) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x81, &value);
    if (err == ESP_OK) {
        *us = ((value & 3) + 1) * 60;
    }
    return err;
}

esp_err_t tg28_esp_set_dcdc_uvp_delay(const tg28_esp_t *pmu, uint16_t us)
{
    if (us < 60 || us > 240 || us % 60) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x81, 3, us / 60 - 1);
}
