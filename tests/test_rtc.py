# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


COMPONENT = Path(__file__).resolve().parents[1] / "components/candis"

I2C_HEADER = """
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length; int device_address; int scl_speed_hz; } i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 7
bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus);
esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                   i2c_master_dev_handle_t *dev);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *reg, size_t reg_size,
                                     uint8_t *output, size_t output_size, int timeout);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout);
"""

# Build the production RTC source against a register-level I2C double.
TEST_SOURCE = """
#include <assert.h>
#include <string.h>
#include "bsp/candis_rtc.h"
#include "candis_internal.h"

static uint8_t registers[256];
static unsigned writes;
static int fail_calendar_writes;
static bool fail_stop_once;
static bool fail_irq_enable_once;
static bool fail_irq_disable_once;
static bool locked;

bool candis_control_enter(void) { if (locked) return false; return locked = true; }
void candis_control_leave(void) { assert(locked); locked = false; }
esp_err_t candis_i2c_acquire_locked(i2c_master_bus_handle_t *bus) { *bus = registers; return ESP_OK; }
esp_err_t candis_i2c_release_locked(i2c_master_bus_handle_t bus) { assert(bus == registers); return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg,
                                   i2c_master_dev_handle_t *dev)
{
    assert(cfg->dev_addr_length == 7 && cfg->device_address == 0x32 && cfg->scl_speed_hz == 100000);
    *dev = bus;
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) { assert(dev == registers); return ESP_OK; }
const char *esp_err_to_name(esp_err_t code) { return "mock status"; }
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *reg, size_t reg_size,
                                     uint8_t *output, size_t output_size, int timeout)
{
    assert(dev == registers && reg_size == 1 && timeout == 100);
    memcpy(output, registers + *reg, output_size);
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t size, int timeout)
{
    assert(dev == registers && size > 1 && timeout == 100);
    ++writes;
    unsigned reg = data[0];
    if (reg == 0x10) {
        assert(registers[0x1e] & 0x40);
        if (fail_calendar_writes) {
            --fail_calendar_writes;
            registers[reg] = data[1];
            return ESP_FAIL;
        }
    }
    if (reg == 0x1d) {
        registers[reg] &= data[1];
        return ESP_OK;
    }
    if (reg == 0x1f) assert(!(data[1] & 0x20));
    memcpy(registers + reg, data + 1, size - 1);
    if (reg == 0x1e && (data[1] & 0x20) && fail_irq_enable_once) {
        fail_irq_enable_once = false;
        return ESP_FAIL;
    }
    if (reg == 0x1e && !(data[1] & 0x20) && fail_irq_disable_once) {
        fail_irq_disable_once = false;
        registers[reg] |= 0x20;
        return ESP_FAIL;
    }
    if (reg == 0x1e && (data[1] & 0x40) && fail_stop_once) {
        fail_stop_once = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

int main(void)
{
    const struct tm leap_day = {.tm_year = 124, .tm_mon = 1, .tm_mday = 29, .tm_hour = 19, .tm_sec = 55};
    struct tm actual;
    registers[0x1d] = 0x26; /* UF, RSF, VLF */
    assert(candis_rtc_get_time(&actual) == ESP_ERR_INVALID_STATE);
    assert(candis_rtc_set_time(&leap_day, false) == ESP_ERR_INVALID_STATE && writes == 0);
    registers[0x1f] = 0x20;
    assert(candis_rtc_set_time(&leap_day, true) == ESP_ERR_INVALID_STATE && writes == 0);
    registers[0x1f] = 0;
    assert(candis_rtc_set_time(&leap_day, true) == ESP_OK);
    assert(registers[0x1d] == 0x24 && registers[0x1e] == 0 && registers[0x1f] == 0);
    assert(candis_rtc_get_time(&actual) == ESP_OK);
    assert(actual.tm_year == 124 && actual.tm_mon == 1 && actual.tm_mday == 29 &&
           actual.tm_wday == 4 && actual.tm_yday == 59 && actual.tm_sec == 55);

    struct tm invalid = leap_day;
    invalid.tm_year = 125;
    unsigned before = writes;
    assert(candis_rtc_set_time(&invalid, false) == ESP_ERR_INVALID_ARG && writes == before);
    registers[0x1c] |= 0x10;
    assert(candis_rtc_set_time(&leap_day, false) == ESP_ERR_INVALID_STATE && writes == before);
    registers[0x1c] &= ~0x10;

    struct tm next = leap_day;
    next.tm_sec = 56;
    fail_stop_once = true;
    assert(candis_rtc_set_time(&next, false) == ESP_FAIL && registers[0x1e] == 0);
    assert(candis_rtc_get_time(&actual) == ESP_OK && actual.tm_sec == 55);
    fail_calendar_writes = 1;
    assert(candis_rtc_set_time(&next, false) == ESP_FAIL && registers[0x1e] == 0);
    assert(candis_rtc_get_time(&actual) == ESP_OK && actual.tm_sec == 55);
    fail_calendar_writes = 2;
    assert(candis_rtc_set_time(&next, false) == ESP_FAIL && registers[0x1e] == 0);
    assert(candis_rtc_get_time(&actual) == ESP_ERR_INVALID_STATE);
    assert(candis_rtc_set_time(&next, false) == ESP_OK);
    assert(candis_rtc_get_time(&actual) == ESP_OK && actual.tm_sec == 56);

    candis_rtc_update_irq_t irq = {0};
    registers[0x1c] = 0x24; /* Minute updates and the idle timer clock select. */
    registers[0x1d] = 0x3c;
    registers[0x1e] = 0x08; /* An existing alarm owns the interrupt. */
    before = writes;
    assert(candis_rtc_arm_update_irq(&irq) == ESP_ERR_INVALID_STATE && writes == before);
    registers[0x1e] = 0;
    assert(candis_rtc_arm_update_irq(&irq) == ESP_OK);
    assert(irq.restore_pending && irq.minute_updates);
    assert(registers[0x1c] == 0x04 && registers[0x1d] == 0x1c && registers[0x1e] == 0x20);
    assert(candis_rtc_arm_update_irq(&irq) == ESP_ERR_INVALID_STATE);
    registers[0x1d] |= 0x20;
    bool updated = false;
    assert(candis_rtc_disarm_update_irq(&irq, &updated) == ESP_OK && updated);
    assert(!irq.restore_pending && registers[0x1c] == 0x24 &&
           registers[0x1d] == 0x1c && registers[0x1e] == 0);

    fail_irq_enable_once = true;
    fail_irq_disable_once = true;
    assert(candis_rtc_arm_update_irq(&irq) == ESP_FAIL);
    assert(irq.restore_pending && registers[0x1e] == 0x20);
    assert(candis_rtc_disarm_update_irq(&irq, NULL) == ESP_OK);
    assert(!irq.restore_pending && registers[0x1c] == 0x24 && registers[0x1e] == 0);
    return 0;
}
"""


class RtcDriverTest(unittest.TestCase):
    def test_calendar_and_io_failure_paths(self):
        with TemporaryDirectory() as work:
            root = Path(work)
            (root / "sdkconfig.h").write_text("#define CONFIG_COMPILER_OPTIMIZATION_PERF 0\n")
            (root / "candis_internal.h").write_text(I2C_HEADER + '\n#include "candis_board.h"\n')
            (root / "esp_check.h").write_text(
                "#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)\n"
                "#define ESP_LOGE(tag, ...) ESP_LOGW(tag, __VA_ARGS__)\n"
                "#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); "
                "esp_err_t e = (call); if (e != ESP_OK) return e; } while (0)\n"
            )
            (root / "freertos").mkdir()
            (root / "freertos/FreeRTOS.h").write_text("#define pdMS_TO_TICKS(ms) (ms)\n")
            (root / "freertos/task.h").write_text("static inline void vTaskDelay(int ticks) { (void)ticks; }\n")
            driver = root / "candis_rtc.c"
            shutil.copyfile(COMPONENT / driver.name, driver)
            source = root / "test_rtc.c"
            source.write_text(TEST_SOURCE)
            idf = Path(os.environ["IDF_PATH"])
            executable = root / "test_rtc"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", f"-I{root}", f"-I{COMPONENT}", f"-I{COMPONENT / 'include'}",
                            f"-I{idf / 'components/esp_common/include'}", str(driver), str(source),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
