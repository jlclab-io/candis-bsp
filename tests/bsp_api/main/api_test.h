/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "api_test_console.h"
#include "tg28_esp.h"

typedef enum {
    API_ASPECT_NORMAL,
    API_ASPECT_BOUNDARY,
    API_ASPECT_ERROR,
    API_ASPECT_LIFECYCLE,
    API_ASPECT_HARDWARE,
    API_ASPECT_COUNT,
} api_aspect_t;

typedef enum {
    API_NOT_RUN,
    API_PASS,
    API_FAIL,
    API_BLOCKED,
    API_NA,
} api_status_t;

void api_test_log(const char *component, const char *api, api_aspect_t aspect,
                  api_status_t status, const char *evidence);
esp_err_t api_test_require(const char *component, const char *api, api_aspect_t aspect,
                           esp_err_t actual, esp_err_t expected, const char *detail);
void api_test_memory(const char *phase);
const char *api_test_idf(void);
bool api_test_check_bool(const char *component, const char *api, api_aspect_t aspect,
                         bool condition, const char *detail);
void api_test_suite_begin(const char *component);
void api_test_suite_end(const char *component);

typedef struct {
    bool enabled;
    uint16_t millivolts;
} api_test_rail_state_t;

/** Test-only, specification-bounded PMU rail setup using public TG28 APIs. */
esp_err_t api_test_rail_capture(tg28_esp_rail_t rail, api_test_rail_state_t *state);
esp_err_t api_test_rail_force(tg28_esp_rail_t rail, uint16_t millivolts, bool enabled);
esp_err_t api_test_rail_restore(tg28_esp_rail_t rail, const api_test_rail_state_t *state);

#define API_RESULT(component, api, aspect, status, evidence) \
    api_test_log((component), (api), (aspect), (status), (evidence))
#define API_BLOCKED(component, api, aspect, evidence) \
    api_test_log((component), (api), (aspect), API_BLOCKED, (evidence))
#define API_NA(component, api, aspect, evidence) \
    api_test_log((component), (api), (aspect), API_NA, (evidence))
