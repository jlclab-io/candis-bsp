/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "api_suites.h"
#include "api_symbols.h"
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    const esp_err_t console = api_test_console_start(15000);
    api_test_emit("API_TEST_BEGIN|idf=%s|target=%s|public_api_manifest=%zu|cdc=%s\n",
                  api_test_idf(), CONFIG_IDF_TARGET, api_symbols_count(),
                  esp_err_to_name(console));
    api_test_memory("boot");
    api_symbols_link_all_public_functions();
    api_tests_candis();
    api_tests_tg28();
    api_tests_dock();
    api_tests_camera();
    esp_err_t i2c_stop = bsp_i2c_deinit();
    api_test_emit("API_I2C_FINAL|%s\n", esp_err_to_name(i2c_stop));
    api_test_memory("final");
    api_test_emit("API_TEST_COMPLETE|idf=%s\n", api_test_idf());
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
