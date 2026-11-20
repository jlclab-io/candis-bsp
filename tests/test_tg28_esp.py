# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest

from test_rtc import I2C_HEADER


COMPONENT = Path(__file__).resolve().parents[1] / "components/tg28-esp"

TEST = r"""
#include <assert.h>
#include <string.h>
#include "tg28_esp.h"

static uint8_t reg[256];
static bool fail_read, fail_write;
static unsigned reads, writes;

static void capture_irq(uint32_t events, void *user)
{
    *(uint32_t *)user = events;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                    i2c_master_dev_handle_t *dev)
{
    assert(cfg->device_address == 0x34);
    *dev = bus;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert(dev == reg);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *command, size_t command_size,
                                      uint8_t *data, size_t size, int timeout)
{
    assert(dev == reg && command_size == 1 && timeout == 30);
    ++reads;
    if (fail_read) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(data, reg + command[0], size);
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout)
{
    assert(dev == reg && size >= 2 && timeout == 30);
    if (fail_write) {
        return ESP_ERR_TIMEOUT;
    }
    ++writes;
    for (size_t i = 1; i < size; ++i) {
        const unsigned address = data[0] + i - 1;
        if (address >= 0x48 && address <= 0x4a) {
            reg[address] &= ~data[i];
        } else {
            reg[address] = data[i];
        }
    }
    return ESP_OK;
}

int main(void)
{
    tg28_esp_t pmu = {0};
    tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    config.timeout_ms = 30;
    reg[0x18] = 0x0a;
    reg[0x61] = 5;
    reg[0x62] = 0xa2;
    reg[0x63] = 0x15;
    reg[0x64] = 3;
    reg[0x15] = 6;
    reg[0x16] = 4;
    reg[0x67] = 0xc4;
    reg[0x68] = 1;
    assert(tg28_esp_open(&pmu, reg, &config) == ESP_OK);
    assert(reads == 0 && writes == 0 && reg[0x62] == 0xa2);
    assert(tg28_esp_check_chip_id(&pmu) == ESP_ERR_INVALID_RESPONSE && writes == 0);
    reg[0x03] = TG28_ESP_CHIP_ID;
    assert(tg28_esp_check_chip_id(&pmu) == ESP_OK && writes == 0);
    tg28_esp_charger_t charger;
    assert(tg28_esp_read_charger(&pmu, &charger) == ESP_OK);
    assert(charger.current_ma == 50 && charger.enabled && charger.detection_enabled);
    assert(charger.input_limit_ma == 1500 && charger.input_limit_mv == 4360);
    assert(charger.voltage_mv == 4200 && charger.precharge_ma == 125 && charger.termination_ma == 125);

    assert(tg28_esp_set_charge_current(&pmu, 200) == ESP_OK && reg[0x62] == 0xa8);
    assert(reg[0x18] == 0x0a && reg[0x61] == 5 && reg[0x63] == 0x15 && reg[0x64] == 3);
    uint16_t current = 0;
    assert(tg28_esp_read_charge_current(&pmu, &current) == ESP_OK && current == 200);
    assert(tg28_esp_set_charge_current(&pmu, 200) == ESP_OK && writes == 1);
    assert(tg28_esp_set_charge_current(&pmu, 201) == ESP_ERR_INVALID_ARG && writes == 1);
    assert(tg28_esp_set_charge_current(&pmu, 1600) == ESP_ERR_INVALID_ARG && writes == 1);
    fail_read = true;
    assert(tg28_esp_set_charge_current(&pmu, 50) == ESP_ERR_TIMEOUT && writes == 1);
    fail_read = false;
    fail_write = true;
    assert(tg28_esp_set_charge_current(&pmu, 50) == ESP_ERR_TIMEOUT && reg[0x62] == 0xa8);
    fail_write = false;
    assert(tg28_esp_set_charge_current(&pmu, 50) == ESP_OK && reg[0x62] == 0xa2);
    assert(tg28_esp_set_charge_current(&pmu, 300) == ESP_OK && reg[0x62] == 0xa9);

    reg[0x00] = 0x38;
    reg[0x01] = 0x32;
    tg28_esp_status_t status;
    assert(tg28_esp_read_status(&pmu, &status) == ESP_OK);
    assert(status.vbus_good && status.battery_present && status.system_on);
    assert(status.direction == TG28_ESP_BATTERY_CHARGING && status.charge_state == TG28_ESP_CONSTANT_CURRENT);
    reg[0x30] = 3;
    reg[0x34] = 0xce;
    reg[0x35] = 0xd8;
    uint32_t uv;
    assert(tg28_esp_read_adc_uv(&pmu, TG28_ESP_ADC_VBAT, &uv) == ESP_OK && uv == 3800000);
    assert(tg28_esp_read_adc_uv(&pmu, TG28_ESP_ADC_VBUS, &uv) == ESP_ERR_INVALID_STATE);
    const tg28_esp_ts_point_t curve[] = {{400000, 30000}, {600000, 20000}};
    int32_t temperature;
    reg[0x36] = 3;
    reg[0x37] = 0xe8;
    assert(tg28_esp_read_ts_temperature(&pmu, curve, 2, &temperature) == ESP_OK && temperature == 25000);
    assert(tg28_esp_read_ts_temperature(&pmu, curve, 1, &temperature) == ESP_ERR_INVALID_ARG);
    reg[0x36] = 0x3f;
    reg[0x37] = 0xff;
    assert(tg28_esp_read_adc_uv(&pmu, TG28_ESP_ADC_TS, &uv) == ESP_ERR_INVALID_RESPONSE);
    assert(tg28_esp_read_ts_temperature(&pmu, curve, 2, &temperature) == ESP_ERR_INVALID_RESPONSE);
    assert(temperature == 25000);
    reg[0x5c] = 0xc2;
    reg[0x5d] = 0x58;
    assert(tg28_esp_read_ts_override_uv(&pmu, &uv) == ESP_OK && uv == 300000);
    reg[0x30] |= TG28_ESP_ADC_DIE;
    reg[0x3c] = 0x1c;
    reg[0x3d] = 0x6a;  /* 7274 */
    assert(tg28_esp_read_die_temperature(&pmu, &temperature) == ESP_OK && temperature == 22000);
    reg[0x3d] = 0x56;
    assert(tg28_esp_read_die_temperature(&pmu, &temperature) == ESP_OK && temperature == 23000);
    reg[0xa2] = 0x11;
    tg28_esp_gauge_config_t gauge;
    assert(tg28_esp_read_gauge_config(&pmu, &gauge) == ESP_OK);
    assert(gauge.enabled && gauge.sram_selected && gauge.parameter_access_enabled);

    reg[0x48] = 0x0c;
    reg[0x49] = 0x88;
    const tg28_esp_irq_t ack = {.bank = {4, 8, 0}};
    assert(tg28_esp_clear_irq(&pmu, &ack) == ESP_OK && reg[0x48] == 8 && reg[0x49] == 0x80);
    uint32_t events = 0;
    assert(tg28_esp_dispatch_irq(&pmu, TG28_ESP_IRQ_VBUS_INSERT, capture_irq, &events) == ESP_OK);
    assert(events == TG28_ESP_IRQ_VBUS_INSERT && reg[0x49] == 0x80);
    assert(tg28_esp_ack_events(&pmu, events) == ESP_OK && reg[0x49] == 0 && reg[0x48] == 8);
    reg[0x40] = 1;
    reg[0x41] = 0x80;
    reg[0x42] = 1;
    events = TG28_ESP_IRQ_NEW_SOC | TG28_ESP_IRQ_PWRON_SHORT | TG28_ESP_IRQ_WATCHDOG;
    assert(tg28_esp_update_irq_events(&pmu, events, true) == ESP_OK);
    assert(reg[0x40] == 0x11 && reg[0x41] == 0x88 && reg[0x42] == 0x81);
    unsigned writes_before = writes;
    assert(tg28_esp_update_irq_events(&pmu, events, true) == ESP_OK && writes == writes_before);
    assert(tg28_esp_update_irq_events(&pmu, 1U << 21, true) == ESP_ERR_INVALID_ARG && writes == writes_before);
    assert(tg28_esp_update_irq_events(&pmu, TG28_ESP_IRQ_PWRON_SHORT, false) == ESP_OK);
    assert(reg[0x41] == 0x80 && reg[0x42] == 0x81);
    reg[0x83] = 0x80;
    assert(tg28_esp_set_rail_voltage(&pmu, TG28_ESP_RAIL_DCDC2, 1200) == ESP_OK && reg[0x83] == 0xc6);
    assert(tg28_esp_set_rail_voltage(&pmu, TG28_ESP_RAIL_DCDC2, 1210) == ESP_ERR_INVALID_ARG);
    assert(tg28_esp_set_rail_voltage(&pmu, TG28_ESP_RAIL_DCDC2, 1220) == ESP_OK && reg[0x83] == 0xc7);
    uint16_t mv;
    assert(tg28_esp_read_rail_voltage(&pmu, TG28_ESP_RAIL_DCDC2, &mv) == ESP_OK && mv == 1220);
    reg[0x82] = 18;
    assert(tg28_esp_read_rail_voltage(&pmu, TG28_ESP_RAIL_DC1SW, &mv) == ESP_OK && mv == 3300);
    assert(tg28_esp_set_rail_voltage(&pmu, TG28_ESP_RAIL_DC1SW, 3300) == ESP_ERR_NOT_SUPPORTED);
    const tg28_esp_sleep_t sleep = {.irq_wakeup = true, .restore_previous_voltage = true};
    writes_before = writes;
    assert(tg28_esp_request_wakeup(&pmu, &sleep) == ESP_ERR_INVALID_STATE && writes == writes_before);
    assert(tg28_esp_enter_sleep(&pmu, &sleep) == ESP_OK && reg[0x26] == 0x15);
    fail_read = true;
    writes_before = writes;
    assert(tg28_esp_request_wakeup(&pmu, &sleep) == ESP_ERR_TIMEOUT && writes == writes_before);
    fail_read = false;
    assert(tg28_esp_request_wakeup(&pmu, &sleep) == ESP_OK && reg[0x26] == 0x17);
    bool sleeping, waking;
    assert(tg28_esp_read_sleep_controls(&pmu, &sleeping, &waking) == ESP_OK && sleeping && waking);
    assert(tg28_esp_configure_watchdog(&pmu, 32, TG28_ESP_WATCHDOG_IRQ) == ESP_OK && reg[0x19] == 5);
    assert(tg28_esp_feed_watchdog(&pmu) == ESP_OK && reg[0x19] == 13);
    assert(tg28_esp_configure_watchdog(&pmu, 32, TG28_ESP_WATCHDOG_SYSTEM_RESET) == ESP_ERR_INVALID_STATE);
    reg[0x19] = 0x15;
    writes_before = writes;
    assert(tg28_esp_set_watchdog_enabled(&pmu, true) == ESP_ERR_INVALID_STATE && writes == writes_before);
    assert(tg28_esp_request_power(&pmu, TG28_ESP_POWER_OFF) == ESP_ERR_INVALID_STATE);
    assert(tg28_esp_set_dcdc_protection(&pmu, false, 0) == ESP_ERR_INVALID_STATE && writes == writes_before);
    assert(tg28_esp_set_backup_charge_enabled(&pmu, true) == ESP_ERR_INVALID_STATE);
    assert(tg28_esp_reset_fuel_gauge(&pmu) == ESP_ERR_INVALID_STATE);
    assert(tg28_esp_close(&pmu) == ESP_OK);

    config.allow_protection_changes = true;
    assert(tg28_esp_open(&pmu, reg, &config) == ESP_OK);
    reg[0x10] = 0x32;
    assert(tg28_esp_request_power(&pmu, TG28_ESP_POWER_OFF) == ESP_OK && reg[0x10] == 0x31);
    reg[0x23] = 0x10;
    assert(tg28_esp_set_dcdc_protection(&pmu, true, 0x0d) == ESP_OK && reg[0x23] == 0x3d);
    bool ovp;
    uint8_t uvp;
    assert(tg28_esp_read_dcdc_protection(&pmu, &ovp, &uvp) == ESP_OK && ovp && uvp == 0x0d);
    assert(tg28_esp_set_dvm(&pmu, TG28_ESP_RAIL_DCDC2, false) == ESP_OK && reg[0x83] == 0x47);
    assert(tg28_esp_set_dvm(&pmu, TG28_ESP_RAIL_DCDC2, true) == ESP_OK && reg[0x83] == 0xc7);
    assert(tg28_esp_set_backup_charge_voltage(&pmu, 2900) == ESP_OK && reg[0x6a] == 3);
    assert(tg28_esp_set_backup_charge_enabled(&pmu, true) == ESP_OK && reg[0x18] == 0x0e);
    bool backup;
    assert(tg28_esp_read_backup_charge(&pmu, &backup, &mv) == ESP_OK && backup && mv == 2900);
    const tg28_esp_safety_timer_t timer = {
        .precharge_min = 60, .charge_hr = 8, .precharge_enabled = true, .charge_enabled = true,
        .slow_under_limiting = true,
    };
    reg[0x67] = 8;
    assert(tg28_esp_set_safety_timer(&pmu, &timer) == ESP_OK && reg[0x67] == 0xde);
    assert(tg28_esp_set_ts_threshold(&pmu, TG28_ESP_TS_CHARGE_HOT, 175) == ESP_ERR_INVALID_ARG);
    assert(tg28_esp_set_ts_threshold(&pmu, TG28_ESP_TS_CHARGE_HOT, 176) == ESP_OK && reg[0x55] == 88);
    const uint8_t scratch[4] = {1, 2, 3, 4};
    uint8_t data[4];
    assert(tg28_esp_write_buffer(&pmu, scratch) == ESP_OK);
    assert(tg28_esp_read_buffer(&pmu, data) == ESP_OK && memcmp(data, scratch, sizeof(data)) == 0);
    assert(tg28_esp_set_gpio1(&pmu, true) == ESP_OK && reg[0x1b] == 4);
    assert(tg28_esp_set_gpio1(&pmu, false) == ESP_OK && reg[0x1b] == 0);
    assert(tg28_esp_close(&pmu) == ESP_OK);
}
"""


class TG28Test(unittest.TestCase):
    def test_charge_encoding_and_register_semantics(self):
        with TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "driver").mkdir()
            (work / "driver/i2c_master.h").write_text("#pragma once\n" + I2C_HEADER)
            (work / "sdkconfig.h").write_text("#define CONFIG_COMPILER_OPTIMIZATION_PERF 0\n")
            source = work / "test.c"
            source.write_text(TEST)
            idf_headers = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
            executable = work / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{work}", f"-I{idf_headers}",
                 f"-I{COMPONENT / 'include'}", str(source),
                 *map(str, sorted(COMPONENT.glob("*.c"))), "-o", str(executable)],
                check=True,
            )
            subprocess.run([executable], check=True)
