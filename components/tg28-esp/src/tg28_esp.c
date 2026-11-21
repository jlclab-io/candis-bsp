/* SPDX-License-Identifier: Apache-2.0 */
#include "tg28_esp_private.h"

/* TG28-ESP Datasheet V1.0, sections 6.13.2.75 and 6.13.2.77–82. */
#define TG28_REG_OUTPUTS 0x90
#define TG28_LDO_CODE_MASK 0x1f

esp_err_t tg28_esp_open(tg28_esp_t *pmu, i2c_master_bus_handle_t bus,
                       const tg28_esp_config_t *config)
{
    if (!pmu || !bus || !config || config->address < 0x08 || config->address > 0x77 ||
        !config->scl_speed_hz || config->scl_speed_hz > 400000 || config->timeout_ms <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pmu->device) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->address,
        .scl_speed_hz = config->scl_speed_hz,
    };
    i2c_master_dev_handle_t device;
    esp_err_t result = i2c_master_bus_add_device(bus, &device_config, &device);
    if (result == ESP_OK) {
        *pmu = (tg28_esp_t){
            .device = device,
            .timeout_ms = config->timeout_ms,
            .allow_protection_changes = config->allow_protection_changes,
        };
    }
    return result;
}

esp_err_t tg28_esp_close(tg28_esp_t *pmu)
{
    if (!pmu) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pmu->device) {
        return ESP_OK;
    }
    esp_err_t result = i2c_master_bus_rm_device(pmu->device);
    if (result == ESP_OK) {
        *pmu = (tg28_esp_t){0};
    }
    return result;
}

esp_err_t tg28_read_regs(const tg28_esp_t *pmu, uint8_t reg, uint8_t *data, size_t length)
{
    if (!pmu || !data || !length || length > 256U - reg) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pmu->device) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(pmu->device, &reg, 1, data, length, pmu->timeout_ms);
}

esp_err_t tg28_esp_read_registers(const tg28_esp_t *pmu, uint8_t reg, uint8_t *data, size_t size)
{
    return tg28_read_regs(pmu, reg, data, size);
}

esp_err_t tg28_write_reg(const tg28_esp_t *pmu, uint8_t reg, uint8_t value)
{
    if (!pmu) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pmu->device) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(pmu->device, data, sizeof(data), pmu->timeout_ms);
}

esp_err_t tg28_update_reg(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t previous;
    esp_err_t result = tg28_read_reg(pmu, reg, &previous);
    if (result != ESP_OK) {
        return result;
    }
    const uint8_t next = (previous & ~mask) | (value & mask);
    return previous == next ? ESP_OK : tg28_write_reg(pmu, reg, next);
}

esp_err_t tg28_read_flag(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, bool *enabled)
{
    if (!enabled) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, reg, &value);
    if (err == ESP_OK) {
        *enabled = (value & mask) != 0;
    }
    return err;
}

esp_err_t tg28_require_protection_opt_in(const tg28_esp_t *pmu)
{
    if (!pmu) {
        return ESP_ERR_INVALID_ARG;
    }
    return pmu->device && pmu->allow_protection_changes ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t tg28_update_protected(const tg28_esp_t *pmu, uint8_t reg, uint8_t mask, uint8_t value)
{
    esp_err_t err = tg28_require_protection_opt_in(pmu);
    return err == ESP_OK ? tg28_update_reg(pmu, reg, mask, value) : err;
}

esp_err_t tg28_read_voltage_step(const tg28_esp_t *pmu, uint8_t reg, uint16_t min_mv, uint16_t *mv)
{
    if (!mv) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, reg, &value);
    if (err == ESP_OK) {
        *mv = min_mv + (value & 7) * 100;
    }
    return err;
}

esp_err_t tg28_write_voltage_step(const tg28_esp_t *pmu, uint8_t reg, uint16_t min_mv, uint16_t mv)
{
    if (mv < min_mv || mv > min_mv + 700 || (mv - min_mv) % 100) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_protected(pmu, reg, 7, (mv - min_mv) / 100);
}

esp_err_t tg28_esp_read_outputs(const tg28_esp_t *pmu, uint8_t *enabled)
{
    return tg28_read_reg(pmu, TG28_REG_OUTPUTS, enabled);
}

esp_err_t tg28_esp_set_outputs(const tg28_esp_t *pmu, uint8_t mask, bool enabled)
{
    return tg28_update_reg(pmu, TG28_REG_OUTPUTS, mask, enabled ? mask : 0);
}

esp_err_t tg28_esp_read_ldo_code(const tg28_esp_t *pmu, tg28_esp_ldo_t ldo, uint8_t *code)
{
    if (ldo < TG28_ESP_ALDO1 || ldo > TG28_ESP_BLDO2 || !code) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t result = tg28_read_reg(pmu, ldo, &value);
    if (result == ESP_OK) {
        *code = value & TG28_LDO_CODE_MASK;
    }
    return result;
}

esp_err_t tg28_esp_set_ldo_code(const tg28_esp_t *pmu, tg28_esp_ldo_t ldo, uint8_t code)
{
    if (ldo < TG28_ESP_ALDO1 || ldo > TG28_ESP_BLDO2 || code > TG28_LDO_CODE_MASK) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, ldo, TG28_LDO_CODE_MASK, code);
}

esp_err_t tg28_esp_read_chip_id(const tg28_esp_t *pmu, uint8_t *id)
{
    return tg28_read_reg(pmu, 0x03, id);
}

esp_err_t tg28_esp_check_chip_id(const tg28_esp_t *pmu)
{
    uint8_t id;
    esp_err_t err = tg28_esp_read_chip_id(pmu, &id);
    if (err != ESP_OK) {
        return err;
    }
    return id == TG28_ESP_CHIP_ID ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t tg28_esp_read_status(const tg28_esp_t *pmu, tg28_esp_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[2];
    esp_err_t err = tg28_read_regs(pmu, 0x00, data, sizeof(data));
    if (err == ESP_OK) {
        const unsigned phase = data[1] & 7;
        *status = (tg28_esp_status_t) {
            .vbus_good = data[0] & 0x20,
            .batfet_open = data[0] & 0x10,
            .battery_present = data[0] & 8,
            .battery_active = data[0] & 4,
            .thermal_regulation = data[0] & 2,
            .input_current_limit = data[0] & 1,
            .direction = (data[1] >> 5) & 3,
            .system_on = data[1] & 0x10,
            .input_voltage_limit = data[1] & 8,
            .charge_state = phase < TG28_ESP_CHARGE_STATE_UNKNOWN ? phase : TG28_ESP_CHARGE_STATE_UNKNOWN,
        };
    }
    return err;
}

esp_err_t tg28_esp_read_power_sources(const tg28_esp_t *pmu, uint8_t *power_on, uint8_t *power_off)
{
    if (!power_on || !power_off) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[2];
    esp_err_t err = tg28_read_regs(pmu, 0x20, data, sizeof(data));
    if (err == ESP_OK) {
        *power_on = data[0];
        *power_off = data[1];
    }
    return err;
}

esp_err_t tg28_esp_read_adc_mask(const tg28_esp_t *pmu, uint8_t *enabled)
{
    return tg28_read_reg(pmu, 0x30, enabled);
}

esp_err_t tg28_esp_set_adc_channels(const tg28_esp_t *pmu, uint8_t mask, bool enabled)
{
    if (mask & ~0x3f) {
        return ESP_ERR_INVALID_ARG;
    }
    return tg28_update_reg(pmu, 0x30, mask, enabled ? mask : 0);
}

esp_err_t tg28_esp_read_adc_raw(const tg28_esp_t *pmu, tg28_esp_adc_channel_t channel, uint16_t *raw)
{
    unsigned reg;
    switch (channel) {
    case TG28_ESP_ADC_VBAT: reg = 0x34; break;
    case TG28_ESP_ADC_TS:   reg = 0x36; break;
    case TG28_ESP_ADC_VBUS: reg = 0x38; break;
    case TG28_ESP_ADC_VSYS: reg = 0x3a; break;
    case TG28_ESP_ADC_DIE:  reg = 0x3c; break;
    case TG28_ESP_ADC_GP:   return ESP_ERR_NOT_SUPPORTED;
    default:               return ESP_ERR_INVALID_ARG;
    }
    if (!raw) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t enabled;
    esp_err_t err = tg28_esp_read_adc_mask(pmu, &enabled);
    if (err != ESP_OK) {
        return err;
    }
    if (!(enabled & channel)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t data[2];
    err = tg28_read_regs(pmu, reg, data, sizeof(data));
    if (err == ESP_OK) {
        *raw = ((data[0] & 0x3f) << 8) | data[1];
    }
    return err;
}

esp_err_t tg28_esp_read_adc_uv(const tg28_esp_t *pmu, tg28_esp_adc_channel_t channel, uint32_t *uv)
{
    if (!uv) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t raw;
    esp_err_t err = tg28_esp_read_adc_raw(pmu, channel, &raw);
    if (err == ESP_OK) {
        /* The vendor FAQ identifies TS codes above full-scale as overflow. */
        if (channel == TG28_ESP_ADC_TS && raw > 0x2000) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        const unsigned scale = channel == TG28_ESP_ADC_TS ? 500 : channel == TG28_ESP_ADC_DIE ? 100 : 1000;
        *uv = raw * scale;
    }
    return err;
}

esp_err_t tg28_esp_read_irq(const tg28_esp_t *pmu, tg28_esp_irq_t *irq)
{
    return irq ? tg28_read_regs(pmu, 0x48, irq->bank, sizeof(irq->bank)) : ESP_ERR_INVALID_ARG;
}

esp_err_t tg28_esp_read_irq_enable(const tg28_esp_t *pmu, tg28_esp_irq_t *irq)
{
    return irq ? tg28_read_regs(pmu, 0x40, irq->bank, sizeof(irq->bank)) : ESP_ERR_INVALID_ARG;
}

static esp_err_t write_irq(const tg28_esp_t *pmu, const tg28_esp_irq_t *irq, uint8_t reg)
{
    if (!pmu || !irq || (irq->bank[2] & 0x20)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pmu->device) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t data[] = {reg, irq->bank[0], irq->bank[1], irq->bank[2]};
    return i2c_master_transmit(pmu->device, data, sizeof(data), pmu->timeout_ms);
}

esp_err_t tg28_esp_set_irq_enable(const tg28_esp_t *pmu, const tg28_esp_irq_t *irq)
{
    return write_irq(pmu, irq, 0x40);
}

esp_err_t tg28_esp_clear_irq(const tg28_esp_t *pmu, const tg28_esp_irq_t *irq)
{
    return write_irq(pmu, irq, 0x48);
}

esp_err_t tg28_esp_read_watchdog(const tg28_esp_t *pmu, bool *enabled, uint16_t *seconds,
                                tg28_esp_watchdog_action_t *action)
{
    if (!enabled || !seconds || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t data[2];
    esp_err_t err = tg28_read_regs(pmu, 0x18, data, sizeof(data));
    if (err == ESP_OK) {
        *enabled = data[0] & 1;
        *seconds = 1U << (data[1] & 7);
        *action = (data[1] >> 4) & 3;
    }
    return err;
}

esp_err_t tg28_esp_configure_watchdog(const tg28_esp_t *pmu, uint16_t seconds,
                                     tg28_esp_watchdog_action_t action)
{
    if (!seconds || seconds > 128 || (seconds & (seconds - 1)) ||
        (unsigned)action > TG28_ESP_WATCHDOG_POWER_CYCLE_RAILS) {
        return ESP_ERR_INVALID_ARG;
    }
    unsigned code = 0;
    while (seconds > 1) {
        seconds >>= 1;
        ++code;
    }
    const uint8_t value = (action << 4) | code;
    return action == TG28_ESP_WATCHDOG_IRQ ? tg28_update_reg(pmu, 0x19, 0x3f, value) :
           tg28_update_protected(pmu, 0x19, 0x3f, value);
}

esp_err_t tg28_esp_set_watchdog_enabled(const tg28_esp_t *pmu, bool enabled)
{
    if (enabled) {
        bool resets_system;
        esp_err_t err = tg28_read_flag(pmu, 0x19, 0x30, &resets_system);
        if (err != ESP_OK) {
            return err;
        }
        if (resets_system) {
            err = tg28_require_protection_opt_in(pmu);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return tg28_update_reg(pmu, 0x18, 1, enabled ? 1 : 0);
}

esp_err_t tg28_esp_feed_watchdog(const tg28_esp_t *pmu)
{
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x19, &value);
    return err == ESP_OK ? tg28_write_reg(pmu, 0x19, value | 8) : err;
}

static const uint16_t key_on_ms[] = {128, 512, 1000, 2000};

esp_err_t tg28_esp_read_power_key(const tg28_esp_t *pmu, tg28_esp_power_key_t *timing)
{
    if (!timing) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x27, &value);
    if (err == ESP_OK) {
        *timing = (tg28_esp_power_key_t) {
            .on_ms = key_on_ms[value & 3],
            .irq_ms = 1000 + ((value >> 4) & 3) * 500,
            .off_ms = 4000 + ((value >> 2) & 3) * 2000,
        };
    }
    return err;
}

esp_err_t tg28_esp_set_power_key(const tg28_esp_t *pmu, const tg28_esp_power_key_t *timing)
{
    if (!timing || timing->irq_ms < 1000 || timing->irq_ms > 2500 || timing->irq_ms % 500 ||
        timing->off_ms < 4000 || timing->off_ms > 10000 || timing->off_ms % 2000) {
        return ESP_ERR_INVALID_ARG;
    }
    for (unsigned i = 0; i < sizeof(key_on_ms) / sizeof(key_on_ms[0]); ++i) {
        if (timing->on_ms == key_on_ms[i]) {
            const uint8_t config = ((timing->irq_ms - 1000) / 500 << 4) |
                                   ((timing->off_ms - 4000) / 2000 << 2) | i;
            return tg28_update_reg(pmu, 0x27, 0x3f, config);
        }
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t tg28_esp_read_sleep_config(const tg28_esp_t *pmu, tg28_esp_sleep_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x26, &value);
    if (err == ESP_OK) {
        *config = (tg28_esp_sleep_t) {
            .irq_wakeup = value & 0x10,
            .pull_pwrok_low = value & 8,
            .restore_previous_voltage = value & 4,
        };
    }
    return err;
}

static esp_err_t sleep_command(const tg28_esp_t *pmu, const tg28_esp_sleep_t *config, uint8_t command)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The requested latch combination comes from the PMU sleep/wake sequence. */
    const uint8_t value = (config->irq_wakeup ? 0x10 : 0) | (config->pull_pwrok_low ? 8 : 0) |
                          (config->restore_previous_voltage ? 4 : 0) | command;
    return tg28_write_reg(pmu, 0x26, value);
}

esp_err_t tg28_esp_read_sleep_controls(const tg28_esp_t *pmu, bool *sleep, bool *wake)
{
    if (!sleep || !wake) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x26, &value);
    if (err == ESP_OK) {
        *sleep = value & 1;
        *wake = value & 2;
    }
    return err;
}

esp_err_t tg28_esp_enter_sleep(const tg28_esp_t *pmu, const tg28_esp_sleep_t *config)
{
    return sleep_command(pmu, config, 1);
}

esp_err_t tg28_esp_request_wakeup(const tg28_esp_t *pmu, const tg28_esp_sleep_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    bool armed;
    esp_err_t err = tg28_read_flag(pmu, 0x26, 1, &armed);
    if (err != ESP_OK) {
        return err;
    }
    /* Vendor FAQ §2.13 requires SLEEP_EN to remain set for software wake. */
    return armed ? sleep_command(pmu, config, 3) : ESP_ERR_INVALID_STATE;
}

esp_err_t tg28_esp_read_buffer(const tg28_esp_t *pmu, uint8_t data[4])
{
    return tg28_esp_read_registers(pmu, 0x04, data, 4);
}

esp_err_t tg28_esp_write_buffer(const tg28_esp_t *pmu, const uint8_t data[4])
{
    if (!pmu || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pmu->device) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t packet[] = {0x04, data[0], data[1], data[2], data[3]};
    return i2c_master_transmit(pmu->device, packet, sizeof(packet), pmu->timeout_ms);
}

esp_err_t tg28_esp_read_gpio1(const tg28_esp_t *pmu, bool *drive_low)
{
    if (!drive_low) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t value;
    esp_err_t err = tg28_read_reg(pmu, 0x1b, &value);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t mode = (value >> 2) & 3;
    if (mode > 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *drive_low = mode == 1;
    return ESP_OK;
}

esp_err_t tg28_esp_set_gpio1(const tg28_esp_t *pmu, bool drive_low)
{
    return tg28_update_reg(pmu, 0x1b, 0x0c, drive_low ? 4 : 0);
}

esp_err_t tg28_esp_dispatch_irq(const tg28_esp_t *pmu, uint32_t filter,
                               tg28_esp_irq_callback_t callback, void *user)
{
    if (!callback || (filter & ~TG28_ESP_IRQ_ALL)) {
        return ESP_ERR_INVALID_ARG;
    }
    tg28_esp_irq_t irq;
    esp_err_t err = tg28_esp_read_irq(pmu, &irq);
    if (err == ESP_OK) {
        const uint32_t flags = irq.bank[0] | ((uint32_t)irq.bank[1] << 8) | ((uint32_t)irq.bank[2] << 16);
        const uint32_t events = flags & filter;
        if (events) {
            callback(events, user);
        }
    }
    return err;
}

esp_err_t tg28_esp_ack_events(const tg28_esp_t *pmu, uint32_t events)
{
    if (events & ~TG28_ESP_IRQ_ALL) {
        return ESP_ERR_INVALID_ARG;
    }
    const tg28_esp_irq_t irq = {.bank = {events & 0xff, (events >> 8) & 0xff, events >> 16}};
    return tg28_esp_clear_irq(pmu, &irq);
}

esp_err_t tg28_esp_update_irq_events(const tg28_esp_t *pmu, uint32_t events, bool enabled)
{
    if (!events || (events & ~TG28_ESP_IRQ_ALL)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t reg = 0x40; events; ++reg, events >>= 8) {
        const uint8_t mask = events & 0xff;
        if (mask) {
            esp_err_t err = tg28_update_reg(pmu, reg, mask, enabled ? mask : 0);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}
