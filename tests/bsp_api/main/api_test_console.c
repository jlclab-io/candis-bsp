/* SPDX-License-Identifier: Apache-2.0 */
#include "api_test_console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "bsp/candis_usb.h"
#include "candis_example_usb.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bool s_console_ready;
static bool s_native_console;
static bool s_example_console;

static bool write_native_console(const char *line, size_t bytes)
{
    size_t written = 0;
    unsigned stalled = 0;
    while (written < bytes && stalled < 100) {
        size_t queued = 0;
        const esp_err_t err = candis_usb_device_write(line + written, bytes - written, &queued);
        if (err == ESP_OK && queued > 0) {
            written += queued;
            stalled = 0;
            continue;
        }
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            return false;
        }
        ++stalled;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return written == bytes;
}

static esp_err_t start_usb(unsigned timeout_ms)
{
    s_console_ready = false;
    esp_err_t err = candis_usb_device_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    do {
        size_t queued = 0;
        const char probe = '\n';
        err = candis_usb_device_write(&probe, sizeof(probe), &queued);
        if (err == ESP_OK && queued == sizeof(probe)) {
            s_console_ready = true;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    } while (xTaskGetTickCount() - start < timeout);
    return ESP_ERR_TIMEOUT;
}

esp_err_t api_test_console_start(unsigned timeout_ms)
{
    s_example_console = false;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0)
    esp_err_t err = candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true);
    if (err == ESP_OK) {
        s_example_console = true;
        vTaskDelay(pdMS_TO_TICKS(250));
        return ESP_OK;
    }
#else
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#endif
    err = start_usb(timeout_ms);
    s_native_console = err == ESP_OK;
    return err;
}

esp_err_t api_test_console_restore(unsigned timeout_ms)
{
    (void)api_test_console_stop();
    return api_test_console_start(timeout_ms);
}

esp_err_t api_test_console_stop(void)
{
    esp_err_t err = ESP_OK;
    if (s_example_console) {
        err = candis_example_usb_stop();
        if (err == ESP_OK) {
            s_example_console = false;
        }
    }
    if (s_native_console) {
        const esp_err_t native = candis_usb_device_stop();
        if (err == ESP_OK) {
            err = native;
        }
        if (native == ESP_OK) {
            s_native_console = false;
        }
    }
    s_console_ready = false;
    return err;
}

void api_test_emit(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0) {
        return;
    }

    printf("%s", line);
    fflush(stdout);
    if (s_example_console) {
        /* The example console deliberately uses a small bounded ring buffer.
         * Give its USB worker time to drain each API_CASE record. */
        vTaskDelay(pdMS_TO_TICKS(8));
    } else {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!s_native_console || !s_console_ready) {
        return;
    }

    const size_t bytes = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
    if (!write_native_console(line, bytes)) {
        s_console_ready = false;
    }
}
