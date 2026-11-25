/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include "api_test.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "bsp/candis.h"

static const char *TAG = "api_test";

typedef struct {
    unsigned pass;
    unsigned fail;
    unsigned blocked;
    unsigned na;
} api_case_counts_t;

static api_case_counts_t s_case_counts;

static const char *aspect_name(api_aspect_t aspect)
{
    switch (aspect) {
    case API_ASPECT_NORMAL: return "normal";
    case API_ASPECT_BOUNDARY: return "boundary";
    case API_ASPECT_ERROR: return "error";
    case API_ASPECT_LIFECYCLE: return "lifecycle";
    case API_ASPECT_HARDWARE: return "hardware";
    default: return "unknown";
    }
}

static const char *status_name(api_status_t status)
{
    switch (status) {
    case API_NOT_RUN: return "NOT_RUN";
    case API_PASS: return "PASS";
    case API_FAIL: return "FAIL";
    case API_BLOCKED: return "BLOCKED";
    case API_NA: return "N/A";
    default: return "UNKNOWN";
    }
}

const char *api_test_idf(void)
{
    return IDF_VER;
}

void api_test_log(const char *component, const char *api, api_aspect_t aspect,
                  api_status_t status, const char *evidence)
{
    switch (status) {
    case API_PASS: ++s_case_counts.pass; break;
    case API_FAIL: ++s_case_counts.fail; break;
    case API_BLOCKED: ++s_case_counts.blocked; break;
    case API_NA: ++s_case_counts.na; break;
    default: break;
    }
    api_test_emit("API_CASE|%s|%s|%s|%s|idf=%s|evidence=%s\n", component, api,
                  aspect_name(aspect), status_name(status), api_test_idf(),
                  evidence ? evidence : "");
}

esp_err_t api_test_require(const char *component, const char *api, api_aspect_t aspect,
                           esp_err_t actual, esp_err_t expected, const char *detail)
{
    char evidence[160];
    snprintf(evidence, sizeof(evidence), "actual=%s expected=%s detail=%s",
             esp_err_to_name(actual), esp_err_to_name(expected), detail ? detail : "");
    api_test_log(component, api, aspect, actual == expected ? API_PASS : API_FAIL, evidence);
    return actual;
}

bool api_test_check_bool(const char *component, const char *api, api_aspect_t aspect,
                         bool condition, const char *detail)
{
    api_test_log(component, api, aspect, condition ? API_PASS : API_FAIL,
                 detail ? detail : "");
    return condition;
}

void api_test_memory(const char *phase)
{
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t total = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    api_test_emit("API_MEM|%s|internal=%zu|default=%zu|psram=%zu\n", phase, internal, total, psram);
}

void api_test_suite_begin(const char *component)
{
    s_case_counts = (api_case_counts_t){0};
    api_test_memory(component);
    api_test_emit("API_SUITE_BEGIN|%s|idf=%s\n", component, api_test_idf());
}

void api_test_suite_end(const char *component)
{
    const unsigned total = s_case_counts.pass + s_case_counts.fail +
                           s_case_counts.blocked + s_case_counts.na;
    api_test_emit("API_SUMMARY|%s|total=%u|pass=%u|fail=%u|blocked=%u|na=%u\n",
                  component, total, s_case_counts.pass, s_case_counts.fail,
                  s_case_counts.blocked, s_case_counts.na);
    api_test_memory(component);
    api_test_emit("API_SUITE_END|%s|idf=%s\n", component, api_test_idf());
}

static esp_err_t rail_update(tg28_esp_rail_t rail, const api_test_rail_state_t *target)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "open board I2C for test rail setup");
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "board I2C unavailable");
    tg28_esp_t pmu = {0};
    ESP_RETURN_ON_ERROR(tg28_esp_open(&pmu, bus, &(tg28_esp_config_t)TG28_ESP_CONFIG_DEFAULT()),
                        TAG, "open TG28 for test rail setup");
    esp_err_t err = tg28_esp_set_rail_voltage(&pmu, rail, target->millivolts);
    if (err == ESP_OK) {
        err = tg28_esp_set_rail_enabled(&pmu, rail, target->enabled);
    }
    const esp_err_t close = tg28_esp_close(&pmu);
    return err == ESP_OK ? close : err;
}

esp_err_t api_test_rail_capture(tg28_esp_rail_t rail, api_test_rail_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL rail state");
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "open board I2C for rail capture");
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_STATE, TAG, "board I2C unavailable");
    tg28_esp_t pmu = {0};
    ESP_RETURN_ON_ERROR(tg28_esp_open(&pmu, bus, &(tg28_esp_config_t)TG28_ESP_CONFIG_DEFAULT()),
                        TAG, "open TG28 for rail capture");
    api_test_rail_state_t value = {0};
    esp_err_t err = tg28_esp_read_rail_enabled(&pmu, rail, &value.enabled);
    if (err == ESP_OK) {
        err = tg28_esp_read_rail_voltage(&pmu, rail, &value.millivolts);
    }
    const esp_err_t close = tg28_esp_close(&pmu);
    if (err == ESP_OK) {
        err = close;
    }
    if (err == ESP_OK) {
        *state = value;
    }
    return err;
}

esp_err_t api_test_rail_force(tg28_esp_rail_t rail, uint16_t millivolts, bool enabled)
{
    return rail_update(rail, &(api_test_rail_state_t){.enabled = enabled, .millivolts = millivolts});
}

esp_err_t api_test_rail_restore(tg28_esp_rail_t rail, const api_test_rail_state_t *state)
{
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL rail state");
    return rail_update(rail, state);
}
