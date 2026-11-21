# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


DRIVER = Path(__file__).resolve().parents[1] / "components/candis/src/candis.c"

HEADER = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef void *i2c_master_bus_handle_t;
typedef int esp_err_t;
typedef struct {
    int i2c_port, sda_io_num, scl_io_num, clk_source, glitch_ignore_cnt;
} i2c_master_bus_config_t;

enum {
    ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
    CANDIS_MAIN_I2C = 0, CANDIS_MAIN_SDA = 7, CANDIS_MAIN_SCL = 6, I2C_CLK_SRC_DEFAULT = 0
};

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus);
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_deinit(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_err_t candis_i2c_acquire(i2c_master_bus_handle_t *bus);
esp_err_t candis_i2c_release(i2c_master_bus_handle_t bus);
"""

TEST = r"""
#include <assert.h>
#include "candis_internal.h"

static int instance, starts, stops;
static bool allocated, fail_start, fail_stop;

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus)
{
    assert(!allocated);
    assert(config->sda_io_num == 7 && config->scl_io_num == 6);
    if (fail_start) {
        return ESP_FAIL;
    }
    *bus = &instance;
    allocated = true;
    ++starts;
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus)
{
    assert(allocated && bus == &instance);
    if (fail_stop) {
        return ESP_FAIL;
    }
    allocated = false;
    ++stops;
    return ESP_OK;
}

int main(void)
{
    fail_start = true;
    assert(bsp_i2c_init() == ESP_FAIL && bsp_i2c_get_handle() == NULL);
    fail_start = false;
    assert(bsp_i2c_get_handle() == &instance && bsp_i2c_init() == ESP_OK && starts == 1);

    i2c_master_bus_handle_t direct = NULL;
    assert(candis_i2c_release(&instance) == ESP_ERR_INVALID_STATE);
    assert(candis_i2c_acquire(&direct) == ESP_OK && direct == &instance);
    assert(bsp_i2c_deinit() == ESP_OK && bsp_i2c_deinit() == ESP_OK && allocated);
    assert(bsp_i2c_init() == ESP_OK && starts == 1);
    assert(candis_i2c_release(direct) == ESP_OK && allocated);

    fail_stop = true;
    assert(bsp_i2c_deinit() == ESP_FAIL && allocated && bsp_i2c_get_handle() == direct);
    fail_stop = false;
    assert(bsp_i2c_deinit() == ESP_OK && !allocated && stops == 1);
    assert(bsp_i2c_init() == ESP_OK && bsp_i2c_deinit() == ESP_OK && starts == 2 && stops == 2);
}
"""


class BspI2cTest(unittest.TestCase):
    def test_standard_and_candis_references_survive_teardown_failure(self):
        with TemporaryDirectory() as directory:
            work = Path(directory)
            (work / "freertos").mkdir()
            (work / "freertos/FreeRTOS.h").write_text("#define xPortInIsrContext() (0)\n")
            (work / "candis_internal.h").write_text(HEADER)
            (work / "test.c").write_text(TEST)
            shutil.copyfile(DRIVER, work / DRIVER.name)
            executable = work / "test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            f"-I{work}", str(work / DRIVER.name), str(work / "test.c"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
