/* SPDX-License-Identifier: Apache-2.0 */
#include "tg28_esp_private.h"

static const uint16_t input_ma[] = {100, 500, 900, 1000, 1500, 2000};
static const uint16_t charge_mv[] = {0, 4000, 4100, 4200, 4350, 4400};

static unsigned decode_current(unsigned code)
{
    return code <= 8 ? code * 25 : 200 + (code - 8) * 100;
}

esp_err_t tg28_esp_read_charge_current(const tg28_esp_t *pmu, uint16_t *ma)
{
    if (!ma) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data;
    esp_err_t err = tg28_read_reg(pmu, 0x62, &data);
    if (err != ESP_OK) {
        return err;
    }
    const unsigned code = data & 0x1f;
    if (code > 21) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *ma = decode_current(code);
    return ESP_OK;
}

esp_err_t tg28_esp_read_charger(const tg28_esp_t *pmu, tg28_esp_charger_t *charger)
{
    if (!charger) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t input[2], profile[4], protection[2], module, ts, jeita;
    esp_err_t err;
    if ((err = tg28_read_regs(pmu, 0x15, input, sizeof(input))) != ESP_OK ||
        (err = tg28_read_reg(pmu, 0x18, &module)) != ESP_OK ||
        (err = tg28_read_reg(pmu, 0x50, &ts)) != ESP_OK ||
        (err = tg28_read_reg(pmu, 0x58, &jeita)) != ESP_OK ||
        (err = tg28_read_regs(pmu, 0x61, profile, sizeof(profile))) != ESP_OK ||
        (err = tg28_read_regs(pmu, 0x67, protection, sizeof(protection))) != ESP_OK) {
        return err;
    }
    const unsigned input_code = input[1] & 7;
    const unsigned precharge = profile[0] & 15;
    const unsigned cc = profile[1] & 31;
    const unsigned termination = profile[2] & 15;
    const unsigned cv = profile[3] & 7;
    if (input_code >= sizeof(input_ma) / sizeof(input_ma[0]) ||
        precharge > 8 || cc > 21 || termination > 8 || !cv || cv >= sizeof(charge_mv) / sizeof(charge_mv[0])) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *charger = (tg28_esp_charger_t) {
        .input_limit_ma = input_ma[input_code],
        .input_limit_mv = 3880 + (input[0] & 15) * 80,
        .precharge_ma = precharge * 25,
        .current_ma = decode_current(cc),
        .termination_ma = termination * 25,
        .voltage_mv = charge_mv[cv],
        .termination_enabled = profile[2] & 0x10,
        .enabled = module & 2,
        .detection_enabled = protection[1] & 1,
        .ts_protection_selected = !(ts & 0x10),
        .jeita_enabled = jeita & 1,
        .precharge_timer_enabled = protection[0] & 4,
        .charge_timer_enabled = protection[0] & 0x40,
    };
    return ESP_OK;
}

esp_err_t tg28_esp_set_charge_current(const tg28_esp_t *pmu, uint16_t ma)
{
    unsigned code;
    if (ma <= 200 && ma % 25 == 0) {
        code = ma / 25;
    } else if (ma >= 300 && ma <= 1500 && ma % 100 == 0) {
        code = 8 + (ma - 200) / 100;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x62, 0x1f, code);
}

esp_err_t tg28_esp_set_charger_enabled(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_reg(pmu, 0x18, 2, enabled ? 2 : 0);
}

esp_err_t tg28_esp_set_input_current_limit(const tg28_esp_t *pmu, uint16_t ma)
{
    for (unsigned i = 0; i < sizeof(input_ma) / sizeof(input_ma[0]); ++i) {
        if (input_ma[i] == ma) {
            return tg28_update_reg(pmu, 0x16, 7, i);
        }
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t tg28_esp_set_input_voltage_limit(const tg28_esp_t *pmu, uint16_t mv)
{
    if (mv < 3880 || mv > 5080 || (mv - 3880) % 80) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x15, 15, (mv - 3880) / 80);
}

esp_err_t tg28_esp_set_precharge_current(const tg28_esp_t *pmu, uint16_t ma)
{
    if (ma > 200 || ma % 25) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x61, 15, ma / 25);
}

esp_err_t tg28_esp_set_charge_termination(const tg28_esp_t *pmu, uint16_t ma, bool enabled)
{
    if (ma > 200 || ma % 25) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x63, 31, (enabled ? 16 : 0) | (ma / 25));
}

esp_err_t tg28_esp_set_charge_voltage(const tg28_esp_t *pmu, uint16_t mv)
{
    for (unsigned i = 1; i < sizeof(charge_mv) / sizeof(charge_mv[0]); ++i) {
        if (charge_mv[i] == mv) {
            return tg28_update_reg(pmu, 0x64, 7, i);
        }
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t tg28_esp_set_battery_detection(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_reg(pmu, 0x68, 1, enabled ? 1 : 0);
}

esp_err_t tg28_esp_set_fuel_gauge(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_reg(pmu, 0x18, 8, enabled ? 8 : 0);
}

esp_err_t tg28_esp_read_gauge_config(const tg28_esp_t *pmu, tg28_esp_gauge_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t module, source;
    esp_err_t err = tg28_read_reg(pmu, 0x18, &module);
    if (err == ESP_OK) {
        err = tg28_read_reg(pmu, 0xa2, &source);
    }
    if (err == ESP_OK) {
        *config = (tg28_esp_gauge_config_t){
            .enabled = module & 8,
            .sram_selected = source & 0x10,
            .parameter_access_enabled = source & 1,
        };
    }
    return err;
}

esp_err_t tg28_esp_read_soc(const tg28_esp_t *pmu, uint8_t *percent)
{
    if (!percent) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t module;
    esp_err_t err = tg28_read_reg(pmu, 0x18, &module);
    if (err != ESP_OK) {
        return err;
    }
    if (!(module & 8)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t value;
    err = tg28_read_reg(pmu, 0xa4, &value);
    if (err != ESP_OK) {
        return err;
    }
    if (value > 100) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *percent = value;
    return ESP_OK;
}

esp_err_t tg28_esp_read_chgled(const tg28_esp_t *pmu, tg28_esp_chgled_t *led)
{
    if (!led) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x69, &value);
    if (err != ESP_OK) {
        return err;
    }
    const unsigned mode = (value >> 1) & 3;
    if (mode > TG28_ESP_CHGLED_MANUAL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *led = (tg28_esp_chgled_t) {
        .enabled = value & 1,
        .mode = mode,
        .output = (value >> 4) & 3,
    };
    return ESP_OK;
}

esp_err_t tg28_esp_set_chgled(const tg28_esp_t *pmu, const tg28_esp_chgled_t *led)
{
    if (!led || (unsigned)led->mode > TG28_ESP_CHGLED_MANUAL || (unsigned)led->output > TG28_ESP_CHGLED_LOW) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x69, 0x37, (led->output << 4) | (led->mode << 1) | led->enabled);
}

/* TG28-ESP Datasheet V1.0 sections 6.11–6.12, REG [12, 14, 17, 1A, 6A]. */
esp_err_t tg28_esp_read_soc_thresholds(const tg28_esp_t *pmu, uint8_t *level1, uint8_t *level2)
{
    if (!level1 || !level2) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x1a, &value);
    if (err == ESP_OK) {
        *level1 = value & 15;
        *level2 = (value >> 4) + 5;
    }
    return err;
}

esp_err_t tg28_esp_set_soc_thresholds(const tg28_esp_t *pmu, uint8_t level1, uint8_t level2)
{
    if (level1 > 15 || level2 < 5 || level2 > 20) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_write_reg(pmu, 0x1a, ((level2 - 5) << 4) | level1);
}

esp_err_t tg28_esp_reset_fuel_gauge(const tg28_esp_t *pmu)
{
    esp_err_t err = tg28_require_protection_opt_in(pmu);
    uint8_t value;
    if (err == ESP_OK) {
        err = tg28_read_reg(pmu, 0x17, &value);
    }
    return err == ESP_OK ? tg28_write_reg(pmu, 0x17, value | 8) : err;
}

esp_err_t tg28_esp_read_batfet_power_off(const tg28_esp_t *pmu, bool *enabled)
{
    return tg28_read_flag(pmu, 0x12, 8, enabled);
}

esp_err_t tg28_esp_set_batfet_power_off(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_protected(pmu, 0x12, 8, enabled ? 8 : 0);
}

esp_err_t tg28_esp_read_min_system_voltage(const tg28_esp_t *pmu, uint16_t *mv)
{
    return tg28_read_voltage_step(pmu, 0x14, 3200, mv);
}

esp_err_t tg28_esp_set_min_system_voltage(const tg28_esp_t *pmu, uint16_t mv)
{
    return tg28_write_voltage_step(pmu, 0x14, 3200, mv);
}

esp_err_t tg28_esp_read_backup_charge(const tg28_esp_t *pmu, bool *enabled, uint16_t *mv)
{
    if (!enabled || !mv) {
        return ESP_ERR_INVALID_ARG;
    }
    bool active;
    esp_err_t err = tg28_read_flag(pmu, 0x18, 4, &active);
    if (err == ESP_OK) {
        err = tg28_read_voltage_step(pmu, 0x6a, 2600, mv);
    }
    if (err == ESP_OK) {
        *enabled = active;
    }
    return err;
}

esp_err_t tg28_esp_set_backup_charge_enabled(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_protected(pmu, 0x18, 4, enabled ? 4 : 0);
}

esp_err_t tg28_esp_set_backup_charge_voltage(const tg28_esp_t *pmu, uint16_t mv)
{
    return tg28_write_voltage_step(pmu, 0x6a, 2600, mv);
}
