/* SPDX-License-Identifier: Apache-2.0 */
#include "api_suites.h"
#include "api_test.h"
#include "bsp/esp-bsp.h"

void api_tests_candis(void)
{
    api_test_suite_begin("candis");
    api_tests_candis_bus();
    api_tests_candis_power_rtc();
    api_tests_candis_display();
    api_tests_candis_audio();
    api_tests_candis_peripherals();
    api_test_suite_end("candis");
}
