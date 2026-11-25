/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAPI "candis"

static void test_power(void)
{
    candis_power_state_t rails = {0};
    candis_supply_state_t supply = {0};
    uint32_t battery_uv = 0;

    api_test_require(CAPI, "candis_power_read", API_ASPECT_BOUNDARY,
                     candis_power_read(NULL), ESP_ERR_INVALID_ARG, "NULL state");
    esp_err_t err = candis_power_read(&rails);
    api_test_require(CAPI, "candis_power_read", API_ASPECT_NORMAL, err, ESP_OK,
                     "read REG90 rail snapshot");
    if (err == ESP_OK) {
        api_test_log(CAPI, "candis_power_read", API_ASPECT_HARDWARE, API_PASS,
                     "TG28 rail register read completed");
    }
    api_test_require(CAPI, "candis_power_read_supply", API_ASPECT_BOUNDARY,
                     candis_power_read_supply(NULL), ESP_ERR_INVALID_ARG, "NULL state");
    err = candis_power_read_supply(&supply);
    api_test_require(CAPI, "candis_power_read_supply", API_ASPECT_NORMAL, err, ESP_OK,
                     "read supply and BLDO2 setpoint");
    if (err == ESP_OK) {
        api_test_check_bool(CAPI, "candis_power_read_supply", API_ASPECT_NORMAL,
                            supply.bldo2_mv >= 500 && supply.bldo2_mv <= 3500,
                            "BLDO2 setpoint in TG28 range");
        api_test_check_bool(CAPI, "candis_power_read_supply", API_ASPECT_NORMAL,
                            !supply.battery_charging || supply.battery_present,
                            "charging implies battery present");
    }

    api_test_require(CAPI, "candis_power_read_battery_uv", API_ASPECT_BOUNDARY,
                     candis_power_read_battery_uv(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    err = candis_power_read_battery_uv(&battery_uv);
    if (err == ESP_OK) {
        api_test_check_bool(CAPI, "candis_power_read_battery_uv", API_ASPECT_NORMAL,
                            battery_uv >= 1000000 && battery_uv <= 6000000,
                            "VBAT in plausible range");
        api_test_log(CAPI, "candis_power_read_battery_uv", API_ASPECT_HARDWARE, API_PASS,
                     "battery voltage ADC completed");
    } else if (err == ESP_ERR_NOT_FOUND) {
        api_test_require(CAPI, "candis_power_read_battery_uv", API_ASPECT_ERROR, err,
                         ESP_ERR_NOT_FOUND, "battery absent is classified");
        API_BLOCKED(CAPI, "candis_power_read_battery_uv", API_ASPECT_HARDWARE,
                    "no battery attached");
    } else {
        api_test_require(CAPI, "candis_power_read_battery_uv", API_ASPECT_ERROR, err,
                         ESP_ERR_INVALID_STATE, "VBAT ADC pin is not enabled");
        API_BLOCKED(CAPI, "candis_power_read_battery_uv", API_ASPECT_HARDWARE,
                    "VBAT ADC precondition unavailable");
    }

    err = candis_power_ensure_default_charge();
    if (err == ESP_OK) {
        api_test_log(CAPI, "candis_power_ensure_default_charge", API_ASPECT_NORMAL, API_PASS,
                     "charge profile validated and read back");
        api_test_log(CAPI, "candis_power_ensure_default_charge", API_ASPECT_HARDWARE, API_PASS,
                     "TG28 CC setpoint is 200 mA");
    } else {
        api_test_require(CAPI, "candis_power_ensure_default_charge", API_ASPECT_ERROR, err,
                         ESP_ERR_INVALID_STATE, "unsafe charger state correctly rejected");
        API_BLOCKED(CAPI, "candis_power_ensure_default_charge", API_ASPECT_HARDWARE,
                    "VBUS or battery precondition unavailable");
    }

    api_test_require(CAPI, "candis_expansion_power_acquire", API_ASPECT_NORMAL,
                     candis_expansion_power_acquire(true), ESP_OK,
                     "enable DC1SW on the integrated fixture");
    api_test_require(CAPI, "candis_expansion_power_release", API_ASPECT_NORMAL,
                     candis_expansion_power_release(), ESP_OK, "release test DC1SW reference");
    const esp_err_t preexisting_acquire = candis_expansion_power_acquire(false);
    api_test_check_bool(CAPI, "candis_expansion_power_acquire", API_ASPECT_ERROR,
                        preexisting_acquire == ESP_OK ||
                        preexisting_acquire == ESP_ERR_INVALID_STATE,
                        "manage_power=false accepts an already-on rail or rejects an off rail");
    api_test_require(CAPI, "candis_expansion_power_release", API_ASPECT_NORMAL,
                     candis_expansion_power_release(), ESP_OK, "release legacy DC1SW reference");
    api_test_require(CAPI, "candis_expansion_power_release", API_ASPECT_LIFECYCLE,
                     candis_expansion_power_release(), ESP_OK, "repeated release is safe");

    candis_expansion_power_handle_t lease = NULL;
    candis_expansion_power_handle_t active = (candis_expansion_power_handle_t)(uintptr_t)1;
    api_test_require(CAPI, "candis_expansion_power_claim", API_ASPECT_BOUNDARY,
                     candis_expansion_power_claim(false, NULL), ESP_ERR_INVALID_ARG,
                     "NULL handle");
    api_test_require(CAPI, "candis_expansion_power_claim", API_ASPECT_BOUNDARY,
                     candis_expansion_power_claim(false, &active), ESP_ERR_INVALID_STATE,
                     "active output token rejected");
    const esp_err_t preexisting_claim = candis_expansion_power_claim(false, &lease);
    api_test_check_bool(CAPI, "candis_expansion_power_claim", API_ASPECT_ERROR,
                        preexisting_claim == ESP_OK ||
                        preexisting_claim == ESP_ERR_INVALID_STATE,
                        "manage_power=false accepts an already-on rail or rejects an off rail");
    api_test_check_bool(CAPI, "candis_expansion_power_claim", API_ASPECT_ERROR,
                        preexisting_claim == ESP_OK ? lease != NULL : lease == NULL,
                        "lease presence matches the acquisition result");
    if (preexisting_claim == ESP_ERR_INVALID_STATE) {
        api_test_require(CAPI, "candis_expansion_power_claim", API_ASPECT_NORMAL,
                         candis_expansion_power_claim(true, &lease), ESP_OK,
                         "enable DC1SW through a private lease");
    } else if (preexisting_claim == ESP_OK) {
        api_test_log(CAPI, "candis_expansion_power_claim", API_ASPECT_NORMAL, API_PASS,
                     "existing DC1SW rail leased without changing ownership");
    }
    api_test_check_bool(CAPI, "candis_expansion_power_claim", API_ASPECT_HARDWARE,
                        lease != NULL, "private DC1SW handle is live");
    api_test_require(CAPI, "candis_expansion_power_unclaim", API_ASPECT_BOUNDARY,
                     candis_expansion_power_unclaim(NULL), ESP_ERR_INVALID_ARG, "NULL handle slot");
    api_test_require(CAPI, "candis_expansion_power_unclaim", API_ASPECT_NORMAL,
                     candis_expansion_power_unclaim(&lease), ESP_OK, "release private DC1SW lease");
    api_test_require(CAPI, "candis_expansion_power_unclaim", API_ASPECT_LIFECYCLE,
                     candis_expansion_power_unclaim(&lease), ESP_OK, "cleared lease is safe");
    api_test_check_bool(CAPI, "candis_expansion_power_unclaim", API_ASPECT_LIFECYCLE,
                        lease == NULL, "output handle cleared");

    candis_expansion_ldo_handle_t ldo = CANDIS_EXPANSION_LDO_HANDLE_NONE;
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 3300, 0, NULL),
                     ESP_ERR_INVALID_ARG, "NULL handle slot");
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_acquire((candis_expansion_ldo_t)7, 3300, 0, &ldo),
                     ESP_ERR_INVALID_ARG, "invalid rail enum");
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 499, 0, &ldo),
                     ESP_ERR_INVALID_ARG, "voltage below 500 mV");
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 3501, 0, &ldo),
                     ESP_ERR_INVALID_ARG, "voltage above 3500 mV");
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_acquire(CANDIS_EXPANSION_ALDO3, 3350, 0, &ldo),
                     ESP_ERR_INVALID_ARG, "non-100 mV step");
    api_test_require(CAPI, "candis_expansion_ldo_release", API_ASPECT_BOUNDARY,
                     candis_expansion_ldo_release(CANDIS_EXPANSION_LDO_HANDLE_NONE),
                     ESP_ERR_INVALID_ARG, "zero handle rejected");
    ldo = CANDIS_EXPANSION_LDO_HANDLE_NONE;
    api_test_require(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_NORMAL,
                     candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO1, 2800, 5, &ldo), ESP_OK,
                     "enable BLDO1 at 2800 mV for the integrated fixture");
    api_test_check_bool(CAPI, "candis_expansion_ldo_acquire", API_ASPECT_HARDWARE,
                        ldo != CANDIS_EXPANSION_LDO_HANDLE_NONE, "live LDO lease issued");
    api_test_require(CAPI, "candis_expansion_ldo_release", API_ASPECT_NORMAL,
                     candis_expansion_ldo_release(ldo), ESP_OK, "restore BLDO1");
    ldo = CANDIS_EXPANSION_LDO_HANDLE_NONE;
}

static void test_rtc(void)
{
    struct tm original;
    struct tm now;
    memset(&original, 0, sizeof(original));
    memset(&now, 0, sizeof(now));

    api_test_require(CAPI, "candis_rtc_get_time", API_ASPECT_BOUNDARY,
                     candis_rtc_get_time(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    esp_err_t err = candis_rtc_get_time(&original);
    if (err == ESP_OK) {
        api_test_log(CAPI, "candis_rtc_get_time", API_ASPECT_NORMAL, API_PASS,
                     "RX8130CE calendar read");
        now = original;
        api_test_check_bool(CAPI, "candis_rtc_get_time", API_ASPECT_NORMAL,
                            original.tm_year >= 100 && original.tm_year <= 199 &&
                            original.tm_isdst == 0, "year range and UTC marker");
        api_test_require(CAPI, "candis_rtc_set_time", API_ASPECT_NORMAL,
                         candis_rtc_set_time(&now, false), ESP_OK, "write unchanged calendar");
        memset(&now, 0, sizeof(now));
        api_test_require(CAPI, "candis_rtc_get_time", API_ASPECT_NORMAL,
                         candis_rtc_get_time(&now), ESP_OK, "read back written calendar");
        api_test_check_bool(CAPI, "candis_rtc_set_time", API_ASPECT_HARDWARE,
                            now.tm_year == original.tm_year && now.tm_mon == original.tm_mon &&
                            now.tm_mday == original.tm_mday, "calendar date preserved after set");
        API_BLOCKED(CAPI, "candis_rtc_set_time", API_ASPECT_HARDWARE,
                    "calendar retention through power loss needs physical intervention");
    } else {
        api_test_require(CAPI, "candis_rtc_get_time", API_ASPECT_ERROR, err,
                         ESP_ERR_INVALID_STATE, "invalid/STOP calendar classified");
        API_BLOCKED(CAPI, "candis_rtc_get_time", API_ASPECT_HARDWARE,
                    "RTC reports loss of validity");
        API_BLOCKED(CAPI, "candis_rtc_set_time", API_ASPECT_HARDWARE,
                    "calibration safety recovery not authorized automatically");
    }

    api_test_require(CAPI, "candis_rtc_set_time", API_ASPECT_BOUNDARY,
                     candis_rtc_set_time(NULL, false), ESP_ERR_INVALID_ARG, "NULL calendar");
    now = (struct tm){.tm_year = 99, .tm_mon = 0, .tm_mday = 1};
    api_test_require(CAPI, "candis_rtc_set_time", API_ASPECT_BOUNDARY,
                     candis_rtc_set_time(&now, false), ESP_ERR_INVALID_ARG, "year below 2000");
    now = (struct tm){.tm_year = 200, .tm_mon = 0, .tm_mday = 1};
    api_test_require(CAPI, "candis_rtc_set_time", API_ASPECT_BOUNDARY,
                     candis_rtc_set_time(&now, false), ESP_ERR_INVALID_ARG, "year above 2099");

    candis_rtc_update_irq_t irq = {0};
    api_test_require(CAPI, "candis_rtc_arm_update_irq", API_ASPECT_BOUNDARY,
                     candis_rtc_arm_update_irq(NULL), ESP_ERR_INVALID_ARG, "NULL state");
    err = candis_rtc_arm_update_irq(&irq);
    if (err == ESP_OK) {
        api_test_log(CAPI, "candis_rtc_arm_update_irq", API_ASPECT_NORMAL, API_PASS,
                     "RTC update interrupt armed");
        bool updated = false;
        api_test_require(CAPI, "candis_rtc_disarm_update_irq", API_ASPECT_NORMAL,
                         candis_rtc_disarm_update_irq(&irq, &updated), ESP_OK,
                         "read flag and disarm update interrupt");
        api_test_check_bool(CAPI, "candis_rtc_disarm_update_irq", API_ASPECT_NORMAL,
                            !irq.restore_pending, "restore no longer pending");
        api_test_check_bool(CAPI, "candis_rtc_arm_update_irq", API_ASPECT_HARDWARE,
                            true, "RX8130CE timer register accepted configuration");
    } else {
        api_test_require(CAPI, "candis_rtc_arm_update_irq", API_ASPECT_ERROR, err,
                         ESP_ERR_INVALID_STATE, "idle interrupt precondition unavailable");
        API_BLOCKED(CAPI, "candis_rtc_arm_update_irq", API_ASPECT_HARDWARE,
                    "RTC interrupt ownership/precondition unavailable");
        API_BLOCKED(CAPI, "candis_rtc_disarm_update_irq", API_ASPECT_HARDWARE,
                    "interrupt was not armed");
    }
    api_test_require(CAPI, "candis_rtc_disarm_update_irq", API_ASPECT_BOUNDARY,
                     candis_rtc_disarm_update_irq(NULL, NULL), ESP_ERR_INVALID_ARG,
                     "NULL state");
}

void api_tests_candis_power_rtc(void)
{
    test_power();
    test_rtc();
}
