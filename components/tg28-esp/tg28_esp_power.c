/* SPDX-License-Identifier: Apache-2.0 */
#include "tg28_esp_private.h"

/* TG28-ESP Datasheet V1.0 sections 6.5.4 and 6.13.2.18–29. */
esp_err_t tg28_esp_request_power(const tg28_esp_t *pmu, tg28_esp_power_command_t command)
{
    if ((unsigned)command > TG28_ESP_POWER_RESTART) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = tg28_require_protection_opt_in(pmu);
    uint8_t value;
    if (err == ESP_OK) {
        err = tg28_read_reg(pmu, 0x10, &value);
    }
    /* REG10[1:0] are commands. Never replay the other command. */
    return err == ESP_OK ? tg28_write_reg(pmu, 0x10, (value & ~3) | (1U << command)) : err;
}

esp_err_t tg28_esp_read_power_control(const tg28_esp_t *pmu, tg28_esp_power_control_t *control)
{
    if (!control) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x10, &value);
    if (err == ESP_OK) {
        *control = (tg28_esp_power_control_t) {
            .discharge_outputs = value & 0x20,
            .pwrok_reset = value & 8,
            .pwron_16s_shutdown = value & 4,
        };
    }
    return err;
}

esp_err_t tg28_esp_set_power_control(const tg28_esp_t *pmu, const tg28_esp_power_control_t *control)
{
    if (!control) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t value = (control->discharge_outputs << 5) | (control->pwrok_reset << 3) |
                          (control->pwron_16s_shutdown << 2);
    return tg28_update_protected(pmu, 0x10, 0x2f, value);
}

esp_err_t tg28_esp_read_shutdown_control(const tg28_esp_t *pmu, tg28_esp_shutdown_control_t *control)
{
    if (!control) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x22, &value);
    if (err == ESP_OK) {
        *control = (tg28_esp_shutdown_control_t) {
            .die_shutdown = value & 4,
            .key_shutdown = value & 2,
            .key_restarts = value & 1,
        };
    }
    return err;
}

esp_err_t tg28_esp_set_shutdown_control(const tg28_esp_t *pmu, const tg28_esp_shutdown_control_t *control)
{
    if (!control) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t value = (control->die_shutdown << 2) | (control->key_shutdown << 1) | control->key_restarts;
    return tg28_update_protected(pmu, 0x22, 7, value);
}

esp_err_t tg28_esp_read_dcdc_protection(const tg28_esp_t *pmu, bool *ovp, uint8_t *uvp)
{
    if (!ovp || !uvp) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x23, &value);
    if (err == ESP_OK) {
        *ovp = value & 0x20;
        *uvp = value & 0x0f;
    }
    return err;
}

esp_err_t tg28_esp_set_dcdc_protection(const tg28_esp_t *pmu, bool ovp, uint8_t uvp)
{
    if (uvp > 0x0f) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, 0x23, 0x2f, (ovp << 5) | uvp);
}

esp_err_t tg28_esp_read_shutdown_voltage(const tg28_esp_t *pmu, uint16_t *mv)
{
    return tg28_read_voltage_step(pmu, 0x24, 2600, mv);
}

esp_err_t tg28_esp_set_shutdown_voltage(const tg28_esp_t *pmu, uint16_t mv)
{
    return tg28_write_voltage_step(pmu, 0x24, 2600, mv);
}

esp_err_t tg28_esp_read_pwrok(const tg28_esp_t *pmu, tg28_esp_pwrok_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x25, &value);
    if (err == ESP_OK) {
        *config = (tg28_esp_pwrok_t) {
            .delay_ms = 8U << (value & 3),
            .check_after_128ms = value & 0x10,
            .shutdown_delay_4ms = value & 8,
            .reverse_shutdown_order = value & 4,
        };
    }
    return err;
}

esp_err_t tg28_esp_set_pwrok(const tg28_esp_t *pmu, const tg28_esp_pwrok_t *config)
{
    if (!config || config->delay_ms < 8 || config->delay_ms > 64 ||
        (config->delay_ms & (config->delay_ms - 1))) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t code = 0;
    for (uint8_t delay = config->delay_ms; delay > 8; delay >>= 1) {
        ++code;
    }
    const uint8_t value = (config->check_after_128ms << 4) | (config->shutdown_delay_4ms << 3) |
                          (config->reverse_shutdown_order << 2) | code;
    return tg28_update_protected(pmu, 0x25, 0x1f, value);
}

typedef struct {
    uint8_t reg;
    uint8_t shift;
} sequence_field_t;

static const sequence_field_t sequence_fields[] = {
    [TG28_ESP_RAIL_DCDC1] = {0x28, 0}, [TG28_ESP_RAIL_DCDC2] = {0x28, 2},
    [TG28_ESP_RAIL_DCDC3] = {0x28, 4}, [TG28_ESP_RAIL_DCDC4] = {0x28, 6},
    [TG28_ESP_RAIL_ALDO1] = {0x29, 2}, [TG28_ESP_RAIL_ALDO2] = {0x29, 4},
    [TG28_ESP_RAIL_ALDO3] = {0x29, 6}, [TG28_ESP_RAIL_ALDO4] = {0x2a, 0},
    [TG28_ESP_RAIL_BLDO1] = {0x2a, 2}, [TG28_ESP_RAIL_BLDO2] = {0x2a, 4},
    [TG28_ESP_RAIL_CPUSLDO] = {0x2a, 6},
    [TG28_ESP_RAIL_DC1SW] = {0x2b, 0}, [TG28_ESP_RAIL_DC4SW] = {0x2b, 2},
};

esp_err_t tg28_esp_read_fast_sequence(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint8_t *slot)
{
    if ((unsigned)rail >= sizeof(sequence_fields) / sizeof(sequence_fields[0]) || !slot) {
        return ESP_ERR_INVALID_ARG;
    }
    const sequence_field_t field = sequence_fields[rail];
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, field.reg, &value);
    if (err == ESP_OK) {
        *slot = (value >> field.shift) & 3;
    }
    return err;
}

esp_err_t tg28_esp_set_fast_sequence(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint8_t slot)
{
    if ((unsigned)rail >= sizeof(sequence_fields) / sizeof(sequence_fields[0]) || slot > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    const sequence_field_t field = sequence_fields[rail];
    return tg28_update_protected(pmu, field.reg, 3U << field.shift, slot << field.shift);
}

esp_err_t tg28_esp_read_fast_start(const tg28_esp_t *pmu, bool *power_on, bool *wake)
{
    if (!power_on || !wake) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x2b, &value);
    if (err == ESP_OK) {
        *power_on = value & 0x80;
        *wake = value & 0x40;
    }
    return err;
}

esp_err_t tg28_esp_set_fast_start(const tg28_esp_t *pmu, bool power_on, bool wake)
{
    return tg28_update_protected(pmu, 0x2b, 0xc0, (power_on << 7) | (wake << 6));
}
