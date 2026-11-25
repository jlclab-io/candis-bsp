# SPDX-License-Identifier: Apache-2.0
import shutil
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/candis"

STUB_HEADERS = {
    "esp_err.h": r"""
#pragma once
typedef int esp_err_t;
enum {
    ESP_OK = 0,
    ESP_ERR_INVALID_ARG = 0x102,
    ESP_ERR_INVALID_STATE = 0x103,
    ESP_ERR_INVALID_SIZE = 0x104,
    ESP_ERR_NOT_FOUND = 0x105,
    ESP_ERR_NOT_SUPPORTED = 0x106,
    ESP_ERR_TIMEOUT = 0x107,
    ESP_ERR_INVALID_RESPONSE = 0x108,
    ESP_ERR_INVALID_CRC = 0x109,
};
""",
    "esp_check.h": r"""
#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { \
    (void)(tag); esp_err_t error_ = (call); if (error_ != ESP_OK) return error_; \
} while (0)
""",
    "driver/i2c_master.h": r"""
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;
enum { I2C_ADDR_BIT_LEN_7 = 0 };
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write, size_t write_size,
                                      uint8_t *read, size_t read_size, int timeout_ms);
""",
}

TEST_SOURCE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bsp/candis_baseboard_bus.h"

static int fake_bus;
static int fake_device;
static esp_err_t probe_results[2];
static int probe_calls;
static esp_err_t read_result;
static esp_err_t remove_result;
static uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE];

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms)
{
    assert(bus == &fake_bus && address == 0x50);
    assert((probe_calls == 0 && timeout_ms == 40) ||
           (probe_calls == 1 && timeout_ms == 30));
    assert(probe_calls >= 0 && probe_calls < 2);
    return probe_results[probe_calls++];
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device)
{
    assert(bus == &fake_bus && config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    assert(config->device_address == 0x50 && config->scl_speed_hz == 100000);
    *device = &fake_device;
    return ESP_OK;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    assert(device == &fake_device);
    return remove_result;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write, size_t write_size,
                                      uint8_t *read, size_t read_size, int timeout_ms)
{
    assert(device == &fake_device && write_size == 1 && write[0] == 0);
    assert(read_size == CANDIS_BASEBOARD_IMAGE_SIZE && timeout_ms == 40);
    if (read_result == ESP_OK) {
        memcpy(read, image, sizeof(image));
    }
    return read_result;
}

static candis_capability_state_t classify(esp_err_t *result)
{
    candis_baseboard_info_t identity = {.kind = (candis_baseboard_kind_t)99,
                                        .hardware_revision = 0xdead,
                                        .format_revision = 0xbeef,
                                        .name = "unchanged"};
    candis_capability_state_t state = CANDIS_CAPABILITY_UNSUPPORTED;
    *result = candis_baseboard_probe_state_on_bus(CANDIS_BASEBOARD_BB, &fake_bus, 0x50,
                                                  &state, &identity);
    assert(identity.kind == (candis_baseboard_kind_t)99);
    assert(identity.hardware_revision == 0xdead);
    assert(identity.format_revision == 0xbeef);
    assert(strcmp(identity.name, "unchanged") == 0);
    return state;
}

int main(void)
{
    esp_err_t result;
    memset(image, 0, sizeof(image));
    remove_result = ESP_OK;

    probe_calls = 0;
    probe_results[0] = ESP_ERR_NOT_FOUND;
    assert(classify(&result) == CANDIS_CAPABILITY_NOT_PRESENT && result == ESP_OK);
    assert(probe_calls == 1);

    probe_calls = 0;
    probe_results[0] = ESP_ERR_TIMEOUT;
    assert(classify(&result) == CANDIS_CAPABILITY_NOT_PRESENT && result == ESP_OK);
    assert(probe_calls == 1);

    probe_calls = 0;
    probe_results[0] = ESP_OK;
    probe_results[1] = ESP_OK;
    read_result = ESP_ERR_INVALID_RESPONSE;
    assert(classify(&result) == CANDIS_CAPABILITY_NOT_PRESENT && result == ESP_OK);
    assert(probe_calls == 2);

    probe_calls = 0;
    read_result = ESP_OK;
    assert(classify(&result) == CANDIS_CAPABILITY_NOT_PRESENT && result == ESP_OK);
    assert(probe_calls == 2);

    probe_calls = 0;
    image[0] = 'E';
    image[1] = 'S';
    image[2] = 'P';
    assert(classify(&result) == CANDIS_CAPABILITY_UNSUPPORTED && result == ESP_OK);
    assert(probe_calls == 2);

    probe_calls = 0;
    probe_results[0] = ESP_OK;
    probe_results[1] = ESP_OK;
    read_result = ESP_ERR_INVALID_STATE;
    assert(classify(&result) == CANDIS_CAPABILITY_UNSUPPORTED);
    assert(result == ESP_ERR_INVALID_STATE);
    assert(probe_calls == 2);

    probe_calls = 0;
    probe_results[0] = ESP_ERR_INVALID_RESPONSE;
    assert(classify(&result) == CANDIS_CAPABILITY_NOT_PRESENT && result == ESP_OK);
    assert(probe_calls == 1);
    return 0;
}
"""


class BaseboardStateTest(unittest.TestCase):
    def test_classification_error_mapping(self):
        with TemporaryDirectory() as work:
            root = Path(work)
            (root / "driver").mkdir()
            for name, content in STUB_HEADERS.items():
                (root / name).write_text(content)
            source = root / "test_baseboard_state.c"
            source.write_text(TEST_SOURCE)
            binary = root / "test_baseboard_state"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Werror",
                    f"-I{root}", f"-I{COMPONENT / 'include'}",
                    str(COMPONENT / "src/candis_baseboard.c"),
                    str(COMPONENT / "src/candis_baseboard_bus.c"),
                    str(source), "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
