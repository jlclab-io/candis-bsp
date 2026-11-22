# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest

from test_rtc import I2C_HEADER


COMPONENT = Path(__file__).resolve().parents[1] / 'components/candis'
TG28 = COMPONENT.parent / 'tg28-esp'

TEST_SOURCE = """
#include <assert.h>
#include "bsp/candis_power.h"
#include "candis_internal.h"

enum { DC1SW = 0x80, ALDO3 = 0x04 };
static uint8_t registers[256];
#define enables registers[0x90]
static unsigned bus_users;
static unsigned writes;
static bool nack_next_read;
static bool fail_enable, fail_rollback;

bool candis_control_enter(void) { return true; }
void candis_control_leave(void) { }

esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus)
{
    ++bus_users;
    *bus = &enables;
    return ESP_OK;
}

esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus)
{
    assert(bus == &enables && bus_users);
    --bus_users;
    return ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                   i2c_master_dev_handle_t *dev)
{
    assert(bus == &enables && cfg->dev_addr_length == 7 &&
           cfg->device_address == 0x34 && cfg->scl_speed_hz == 100000);
    *dev = bus;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev)
{
    assert(dev == &enables);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *reg, size_t reg_size,
                                     uint8_t *output, size_t size, int timeout)
{
    assert(dev == &enables && reg_size == 1 && size == 1 && timeout == 100);
    if (nack_next_read) {
        nack_next_read = false;
        return ESP_ERR_INVALID_RESPONSE;
    }
    *output = registers[*reg];
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout)
{
    assert(dev == &enables && size == 2 && timeout == 100);
    if (data[0] == 0x90 && data[1] == 0 && fail_rollback) {
        return ESP_FAIL;
    }
    registers[data[0]] = data[1];
    ++writes;
    return (data[0] == 0x90 && data[1] == ALDO3 && fail_enable) ? ESP_FAIL : ESP_OK;
}

int main(void)
{
    candis_power_t first = {.rail = CANDIS_RAIL_EXPANSION};
    candis_power_t second = {.rail = CANDIS_RAIL_EXPANSION};
    assert(candis_power_acquire(&first, false, 20) == ESP_ERR_INVALID_STATE);
    assert(!writes && bus_users == 1);
    assert(candis_power_acquire(&second, true, 20) == ESP_ERR_INVALID_STATE);
    assert(candis_power_release(&first) == ESP_OK && !bus_users);

    assert(candis_power_acquire(&first, true, 20) == ESP_OK && enables == DC1SW);
    assert(candis_power_acquire(&second, false, 20) == ESP_OK && writes == 1 && bus_users == 1);
    assert(candis_power_release(&first) == ESP_OK && enables == DC1SW && writes == 1);
    assert(candis_power_release(&second) == ESP_OK && !enables && writes == 2 && !bus_users);

    enables = DC1SW;
    assert(candis_power_acquire(&first, true, 20) == ESP_OK);
    assert(candis_power_acquire(&second, false, 20) == ESP_OK);
    assert(candis_power_release(&second) == ESP_OK);
    assert(candis_power_release(&first) == ESP_OK && enables == DC1SW && writes == 2 && !bus_users);

    enables = ALDO3;
    assert(candis_power_acquire(&first, true, 20) == ESP_ERR_INVALID_STATE && enables == ALDO3);
    assert(candis_power_release(&first) == ESP_OK && writes == 2);
    enables = 0;
    assert(candis_power_acquire(&first, true, 20) == ESP_OK);
    enables |= ALDO3;
    assert(candis_power_release(&first) == ESP_ERR_INVALID_STATE && enables == (ALDO3 | DC1SW));
    assert(candis_power_acquire(&second, false, 20) == ESP_ERR_INVALID_STATE);
    enables &= ~ALDO3;
    assert(candis_power_release(&first) == ESP_OK && !enables && !bus_users);

    candis_expansion_ldo_handle_t ldo = NULL;
    candis_expansion_ldo_handle_t duplicate = NULL;
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1550, 5, &ldo) == ESP_ERR_INVALID_ARG);
    assert(!ldo && !bus_users);

    registers[0x94] = 0xa0 | 28;
    nack_next_read = true;
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &ldo) == ESP_OK);
    assert(ldo && enables == ALDO3 && registers[0x94] == (0xa0 | 10));
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &ldo) == ESP_ERR_INVALID_STATE);
    assert(ldo && bus_users == 1 && enables == ALDO3);
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &duplicate) == ESP_ERR_INVALID_STATE);
    assert(!duplicate && bus_users == 1);
    assert(candis_expansion_ldo_release(ldo) == ESP_OK && !enables &&
           registers[0x94] == (0xa0 | 28) && !bus_users);

    enables = ALDO3;
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &ldo) == ESP_ERR_INVALID_STATE);
    assert(!ldo && !bus_users && enables == ALDO3 && registers[0x94] == (0xa0 | 28));
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 3300, 5, &ldo) == ESP_OK);
    assert(candis_expansion_ldo_release(ldo) == ESP_OK && enables == ALDO3 &&
           registers[0x94] == (0xa0 | 28) && !bus_users);

    enables = 0;
    fail_enable = true;
    fail_rollback = true;
    assert(candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 1500, 5, &ldo) == ESP_FAIL);
    assert(ldo && bus_users == 1 && enables == ALDO3);
    fail_rollback = false;
    assert(candis_expansion_ldo_release(ldo) == ESP_OK && !enables &&
           registers[0x94] == (0xa0 | 28) && !bus_users);
    assert(candis_expansion_power_acquire(true) == ESP_OK && enables == DC1SW);
    assert(candis_expansion_power_acquire(true) == ESP_ERR_INVALID_STATE);
    assert(candis_power_acquire(&second, false, 0) == ESP_OK);
    assert(candis_expansion_power_release() == ESP_OK && enables == DC1SW);
    assert(candis_power_release(&second) == ESP_OK && !enables && !bus_users);
    assert(candis_expansion_power_release() == ESP_OK && !bus_users);

    candis_expansion_power_handle_t dock = NULL;
    assert(candis_expansion_power_acquire(true) == ESP_OK && enables == DC1SW);
    assert(candis_expansion_power_claim(false, &dock) == ESP_OK && dock);
    assert(candis_expansion_power_claim(false, &dock) == ESP_ERR_INVALID_STATE && dock);
    assert(candis_expansion_power_release() == ESP_OK && enables == DC1SW);
    assert(candis_expansion_power_unclaim(&dock) == ESP_OK && !dock && !enables && !bus_users);
    assert(candis_expansion_power_unclaim(&dock) == ESP_OK && !bus_users);

    candis_power_t panel = {.rail = CANDIS_RAIL_DISPLAY};
    candis_power_t touch = {.rail = CANDIS_RAIL_DISPLAY};
    assert(candis_power_acquire(&panel, true, 150) == ESP_OK && enables == CANDIS_RAIL_DISPLAY);
    assert(candis_power_acquire(&touch, false, 0) == ESP_OK && bus_users == 1);
    assert(candis_power_release(&panel) == ESP_OK && enables == CANDIS_RAIL_DISPLAY);
    assert(candis_power_release(&touch) == ESP_OK && !enables && !bus_users);
    return 0;
}
"""


class ExpansionPowerTest(unittest.TestCase):
    def test_shared_dc1sw_and_cross_rail_guard(self):
        with TemporaryDirectory() as work:
            root = Path(work)
            (root / 'sdkconfig.h').write_text('#define CONFIG_COMPILER_OPTIMIZATION_PERF 0\n')
            (root / 'driver').mkdir()
            (root / 'driver/i2c_master.h').write_text('#pragma once\n' + I2C_HEADER)
            private = (COMPONENT / 'priv_include/candis_internal.h').read_text().replace('#include "bsp/candis.h"', '')
            (root / 'candis_internal.h').write_text(private)
            (root / 'esp_check.h').write_text(
                '#undef ESP_ERROR_CHECK_WITHOUT_ABORT\n'
                '#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))\n'
                '#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); '
                'esp_err_t e = (call); if (e != ESP_OK) return e; } while (0)\n'
                '#define ESP_RETURN_ON_FALSE(condition, error, tag, ...) do { (void)(tag); '
                'if (!(condition)) return error; } while (0)\n'
                '#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)\n'
            )
            (root / 'freertos').mkdir()
            (root / 'freertos/FreeRTOS.h').write_text('#define pdMS_TO_TICKS(ms) (ms)\n')
            (root / 'freertos/task.h').write_text('static inline void vTaskDelay(int ticks) { (void)ticks; }\n')
            driver = root / 'candis_power.c'
            shutil.copyfile(COMPONENT / 'src' / driver.name, driver)
            source = root / 'test_expansion_power.c'
            source.write_text(TEST_SOURCE)
            idf = Path(os.environ['IDF_PATH'])
            executable = root / 'test_expansion_power'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', f'-I{root}', f'-I{COMPONENT}', f"-I{COMPONENT / 'include'}",
                            f"-I{COMPONENT / 'priv_include'}", f"-I{TG28 / 'include'}",
                            f"-I{TG28 / 'priv_include'}", f"-I{idf / 'components/esp_common/include'}",
                            str(driver), str(TG28 / 'src/tg28_esp.c'), str(TG28 / 'src/tg28_esp_regulator.c'), str(source),
                            '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
