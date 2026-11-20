/* SPDX-License-Identifier: Apache-2.0 */
#include "tg28_esp_private.h"

esp_err_t tg28_esp_read_die_temperature(const tg28_esp_t *pmu, int32_t *millicelsius)
{
    if (!millicelsius) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t raw;
    esp_err_t err = tg28_esp_read_adc_raw(pmu, TG28_ESP_ADC_DIE, &raw);
    if (err == ESP_OK) {
        /* TG28 FAQ V1.0 §2.8. The result is an estimate, not a calibration. */
        *millicelsius = 22000 + (7274 - (int32_t)raw) * 50;
    }
    return err;
}

/* TG28-ESP Datasheet V1.0 sections 6.7.4 and 6.13.2.47–57, 64–65. */
static const uint8_t charge_hours[] = {5, 8, 12, 20};
static const uint8_t bias_ua[] = {20, 40, 50, 60};

static uint8_t find_value(const uint8_t *table, size_t count, uint8_t wanted)
{
    for (size_t i = 0; i < count; ++i) {
        if (table[i] == wanted) {
            return i;
        }
    }
    return UINT8_MAX;
}

esp_err_t tg28_esp_read_safety_timer(const tg28_esp_t *pmu, tg28_esp_safety_timer_t *timer)
{
    if (!timer) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x67, &value);
    if (err == ESP_OK) {
        *timer = (tg28_esp_safety_timer_t) {
            .precharge_min = 40 + (value & 3) * 10,
            .charge_hr = charge_hours[(value >> 4) & 3],
            .precharge_enabled = value & 4,
            .charge_enabled = value & 0x40,
            .slow_under_limiting = value & 0x80,
        };
    }
    return err;
}

esp_err_t tg28_esp_set_safety_timer(const tg28_esp_t *pmu, const tg28_esp_safety_timer_t *timer)
{
    if (!timer || timer->precharge_min < 40 || timer->precharge_min > 70 || timer->precharge_min % 10) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t hours = find_value(charge_hours, sizeof(charge_hours), timer->charge_hr);
    if (hours == UINT8_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t value = (timer->slow_under_limiting << 7) | (timer->charge_enabled << 6) |
                          (hours << 4) | (timer->precharge_enabled << 2) | ((timer->precharge_min - 40) / 10);
    return tg28_update_protected(pmu, 0x67, 0xf7, value);
}

esp_err_t tg28_esp_read_die_protection(const tg28_esp_t *pmu, bool *enabled, uint8_t *celsius)
{
    if (!enabled || !celsius) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x13, &value);
    if (err != ESP_OK) {
        return err;
    }
    const unsigned code = (value >> 1) & 3;
    if (code == 3) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *enabled = value & 1;
    *celsius = 115 + code * 10;
    return ESP_OK;
}

esp_err_t tg28_esp_set_die_protection(const tg28_esp_t *pmu, bool enabled, uint8_t celsius)
{
    if (celsius < 115 || celsius > 135 || (celsius - 115) % 10) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x13, 7, (((celsius - 115) / 10) << 1) | enabled);
}

esp_err_t tg28_esp_read_charge_thermal_limit(const tg28_esp_t *pmu, uint8_t *celsius)
{
    if (!celsius) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x65, &value);
    if (err == ESP_OK) {
        *celsius = 60 + (value & 3) * 20;
    }
    return err;
}

esp_err_t tg28_esp_set_charge_thermal_limit(const tg28_esp_t *pmu, uint8_t celsius)
{
    if (celsius < 60 || celsius > 120 || celsius % 20) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x65, 3, (celsius - 60) / 20);
}

esp_err_t tg28_esp_read_ts(const tg28_esp_t *pmu, tg28_esp_ts_t *ts)
{
    if (!ts) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x50, &value);
    if (err == ESP_OK) {
        *ts = (tg28_esp_ts_t) {
            .external_input = value & 0x10,
            .bias_mode = (value >> 2) & 3,
            .bias_ua = bias_ua[value & 3],
        };
    }
    return err;
}

esp_err_t tg28_esp_set_ts(const tg28_esp_t *pmu, const tg28_esp_ts_t *ts)
{
    if (!ts || (unsigned)ts->bias_mode > TG28_ESP_TS_BIAS_ALWAYS) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t bias = find_value(bias_ua, sizeof(bias_ua), ts->bias_ua);
    if (bias == UINT8_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x50, 0x1f, (ts->external_input << 4) | (ts->bias_mode << 2) | bias);
}

esp_err_t tg28_esp_read_ts_override_uv(const tg28_esp_t *pmu, uint32_t *uv)
{
    if (!uv) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[2];
    esp_err_t err = tg28_read_regs(pmu, 0x5c, data, sizeof(data));
    if (err == ESP_OK) {
        *uv = (((uint32_t)(data[0] & 0x3f) << 8) | data[1]) * 500;
    }
    return err;
}

esp_err_t tg28_esp_read_ts_temperature(const tg28_esp_t *pmu, const tg28_esp_ts_point_t *curve,
                                       size_t count, int32_t *millicelsius)
{
    if (!curve || count < 2 || !millicelsius) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 1; i < count; ++i) {
        if (curve[i - 1].uv >= curve[i].uv || curve[i].uv > 0x2000 * 500U) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    uint32_t uv;
    esp_err_t err = tg28_esp_read_adc_uv(pmu, TG28_ESP_ADC_TS, &uv);
    if (err != ESP_OK) {
        return err;
    }
    if (uv < curve[0].uv || uv > curve[count - 1].uv) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    size_t upper = 1;
    while (uv > curve[upper].uv) {
        ++upper;
    }
    const tg28_esp_ts_point_t low = curve[upper - 1];
    const tg28_esp_ts_point_t high = curve[upper];
    const int64_t delta = (int64_t)high.millicelsius - low.millicelsius;
    *millicelsius = low.millicelsius + delta * (uv - low.uv) / (high.uv - low.uv);
    return ESP_OK;
}

static const struct {
    uint8_t reg;
    uint8_t step_mv;
} ts_fields[] = {
    [TG28_ESP_TS_COLD_HYSTERESIS] = {0x52, 16}, [TG28_ESP_TS_HOT_HYSTERESIS] = {0x53, 4},
    [TG28_ESP_TS_CHARGE_COLD] = {0x54, 32}, [TG28_ESP_TS_CHARGE_HOT] = {0x55, 2},
    [TG28_ESP_TS_WORK_COLD] = {0x56, 32}, [TG28_ESP_TS_WORK_HOT] = {0x57, 2},
    [TG28_ESP_TS_JEITA_COOL] = {0x5a, 16}, [TG28_ESP_TS_JEITA_WARM] = {0x5b, 8},
};

esp_err_t tg28_esp_read_ts_threshold(const tg28_esp_t *pmu, tg28_esp_ts_threshold_t threshold, uint16_t *mv)
{
    if ((unsigned)threshold >= sizeof(ts_fields) / sizeof(ts_fields[0]) || !mv) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, ts_fields[threshold].reg, &value);
    if (err == ESP_OK) {
        *mv = value * ts_fields[threshold].step_mv;
    }
    return err;
}

esp_err_t tg28_esp_set_ts_threshold(const tg28_esp_t *pmu, tg28_esp_ts_threshold_t threshold, uint16_t mv)
{
    if ((unsigned)threshold >= sizeof(ts_fields) / sizeof(ts_fields[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    const unsigned step = ts_fields[threshold].step_mv;
    if (mv % step || mv / step > UINT8_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, ts_fields[threshold].reg, 0xff, mv / step);
}

esp_err_t tg28_esp_read_jeita_enabled(const tg28_esp_t *pmu, bool *enabled)
{
    return tg28_read_flag(pmu, 0x58, 1, enabled);
}

esp_err_t tg28_esp_set_jeita_enabled(const tg28_esp_t *pmu, bool enabled)
{
    return tg28_update_protected(pmu, 0x58, 1, enabled);
}

esp_err_t tg28_esp_read_jeita(const tg28_esp_t *pmu, tg28_esp_jeita_t *derating)
{
    if (!derating) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x59, &value);
    if (err != ESP_OK) {
        return err;
    }
    const unsigned cool = value & 3;
    const unsigned warm = (value >> 2) & 3;
    if (cool == 3 || warm == 3) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *derating = (tg28_esp_jeita_t) {
        .cool_half_current = value & 0x10,
        .warm_half_current = value & 0x40,
        .cool_voltage_steps = cool,
        .warm_voltage_steps = warm,
    };
    return ESP_OK;
}

esp_err_t tg28_esp_set_jeita(const tg28_esp_t *pmu, const tg28_esp_jeita_t *derating)
{
    if (!derating || derating->cool_voltage_steps > 2 || derating->warm_voltage_steps > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t value = (derating->warm_half_current << 6) | (derating->cool_half_current << 4) |
                          (derating->warm_voltage_steps << 2) | derating->cool_voltage_steps;
    return tg28_update_protected(pmu, 0x59, 0x5f, value);
}
