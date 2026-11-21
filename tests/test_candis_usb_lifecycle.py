# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


COMPONENT = Path(__file__).resolve().parents[1] / "components/candis"

STUB = r"""
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct { int port; } tinyusb_config_t;
typedef struct { int cdc_port; } tinyusb_config_cdcacm_t;
typedef int tinyusb_cdcacm_itf_t;
enum { TINYUSB_CDC_ACM_0 };
#define TINYUSB_DEFAULT_CONFIG() { .port = 0 }

#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); \
    const esp_err_t error = (call); if (error != ESP_OK) return error; } while (0)
#undef ESP_ERROR_CHECK_WITHOUT_ABORT
#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))

bool candis_control_enter(void);
void candis_control_leave(void);
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config);
esp_err_t tinyusb_cdcacm_deinit(int port);
esp_err_t tinyusb_cdcacm_read(int port, uint8_t *buffer, size_t capacity, size_t *length);
size_t tinyusb_cdcacm_write_queue(int port, const uint8_t *buffer, size_t length);
esp_err_t tinyusb_cdcacm_write_flush(int port, uint32_t timeout);
bool tud_cdc_n_connected(int port);
"""

SOURCE = r"""
#include <assert.h>
#include "bsp/candis_usb.h"
#include "usb_stub.h"

static bool locked, driver, cdc;
static bool fail_driver_stop, fail_cdc_start, fail_cdc_stop;
static unsigned installs, driver_stops, cdc_stops;

bool candis_control_enter(void)
{
    assert(!locked);
    locked = true;
    return true;
}

void candis_control_leave(void)
{
    assert(locked);
    locked = false;
}

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config)
{
    assert(locked && !driver && !cdc && config->port == 0);
    driver = true;
    ++installs;
    return ESP_OK;
}

esp_err_t tinyusb_driver_uninstall(void)
{
    assert(locked && driver);
    ++driver_stops;
    if (fail_driver_stop) {
        return ESP_ERR_TIMEOUT;
    }
    driver = false;
    return ESP_OK;
}

esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config)
{
    assert(locked && driver && !cdc && config->cdc_port == TINYUSB_CDC_ACM_0);
    if (fail_cdc_start) {
        return ESP_ERR_NO_MEM;
    }
    cdc = true;
    return ESP_OK;
}

esp_err_t tinyusb_cdcacm_deinit(int port)
{
    assert(locked && !driver && cdc && port == TINYUSB_CDC_ACM_0);
    ++cdc_stops;
    if (fail_cdc_stop) {
        return ESP_FAIL;
    }
    cdc = false;
    return ESP_OK;
}

esp_err_t tinyusb_cdcacm_read(int port, uint8_t *buffer, size_t capacity, size_t *length)
{
    assert(locked && driver && cdc && port == TINYUSB_CDC_ACM_0 && capacity);
    buffer[0] = 'c';
    *length = 1;
    return ESP_OK;
}

size_t tinyusb_cdcacm_write_queue(int port, const uint8_t *buffer, size_t length)
{
    assert(locked && driver && cdc && port == TINYUSB_CDC_ACM_0 && buffer[0] == 'c');
    return length;
}

esp_err_t tinyusb_cdcacm_write_flush(int port, uint32_t timeout)
{
    assert(locked && driver && cdc && port == TINYUSB_CDC_ACM_0 && timeout == 0);
    return ESP_OK;
}

bool tud_cdc_n_connected(int port)
{
    assert(locked && driver && cdc && port == TINYUSB_CDC_ACM_0);
    return true;
}

int main(void)
{
    uint8_t byte = 0;
    size_t count;
    assert(candis_usb_device_start() == ESP_OK);
    assert(candis_usb_device_start() == ESP_OK && installs == 1);
    assert(candis_usb_device_read(&byte, 1, &count) == ESP_OK && count == 1 && byte == 'c');
    assert(candis_usb_device_write(&byte, 1, &count) == ESP_OK && count == 1);

    fail_driver_stop = true;
    assert(candis_usb_device_stop() == ESP_ERR_TIMEOUT && driver && cdc && cdc_stops == 0);
    assert(candis_usb_device_start() == ESP_ERR_INVALID_STATE);
    assert(candis_usb_device_read(&byte, 1, &count) == ESP_ERR_INVALID_STATE && count == 0);
    assert(candis_usb_device_write(&byte, 1, &count) == ESP_ERR_INVALID_STATE && count == 0);

    fail_driver_stop = false;
    fail_cdc_stop = true;
    assert(candis_usb_device_stop() == ESP_FAIL && !driver && cdc);
    assert(candis_usb_device_start() == ESP_ERR_INVALID_STATE);
    fail_cdc_stop = false;
    assert(candis_usb_device_stop() == ESP_OK && !driver && !cdc);
    assert(candis_usb_device_stop() == ESP_OK && driver_stops == 2 && cdc_stops == 2);

    fail_cdc_start = true;
    fail_driver_stop = true;
    assert(candis_usb_device_start() == ESP_ERR_NO_MEM && driver && !cdc);
    assert(candis_usb_device_start() == ESP_ERR_INVALID_STATE);
    fail_driver_stop = false;
    assert(candis_usb_device_stop() == ESP_OK && !driver && !cdc && cdc_stops == 2);

    fail_cdc_start = false;
    assert(candis_usb_device_start() == ESP_OK && driver && cdc && installs == 3);
    assert(candis_usb_device_stop() == ESP_OK && !driver && !cdc && !locked);
}
"""


class UsbLifecycleTest(unittest.TestCase):
    def test_device_stop_order_and_recovery(self):
        idf = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sdkconfig.h").write_text(
                "#define CONFIG_CANDIS_USB_DEVICE 1\n#define CONFIG_CANDIS_USB_HOST 0\n"
            )
            (root / "usb_stub.h").write_text(STUB)
            for header in ("candis_internal.h", "esp_check.h", "tinyusb.h",
                           "tinyusb_default_config.h", "tinyusb_cdc_acm.h", "tusb.h"):
                (root / header).write_text('#include "usb_stub.h"\n')
            test = root / "test.c"
            test.write_text(SOURCE)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{root}",
                 f"-I{idf}", f"-I{COMPONENT / 'include'}",
                 str(COMPONENT / "src/candis_usb.c"), str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
