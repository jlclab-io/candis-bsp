/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "bsp/candis_motor.h"
#include "bsp/candis_power.h"

static bool s_download_guard;

static void report_rail(void)
{
    candis_supply_state_t supply;
    candis_motor_state_t state;
    esp_err_t err = candis_power_read_supply(&supply);
    if (err == ESP_OK) {
        err = candis_motor_get_state(&state);
    }
    if (err == ESP_OK) {
        printf("Motor supply %u mV, rail %s, pulse %s\n", supply.motor_rail_mv,
               supply.motor_rail_on ? "on" : "off", state.active ? "active" : "idle");
    } else {
        printf("Motor status: %s\n", esp_err_to_name(err));
    }
}

static void release_download_guard(void)
{
    candis_motor_state_t state;
    if (s_download_guard && candis_motor_get_state(&state) == ESP_OK && !state.active && !state.cleanup_pending) {
        candis_example_usb_end_operation();
        s_download_guard = false;
        if (state.last_error != ESP_OK) {
            printf("Motor cleanup: %s\n", esp_err_to_name(state.last_error));
        }
    }
}

static esp_err_t start_pulse(unsigned percent)
{
    if (s_download_guard || !candis_example_usb_begin_operation()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_download_guard = true;
    const esp_err_t err = candis_motor_pulse(percent, CANDIS_MOTOR_MAX_PULSE_MS);
    release_download_guard();
    return err;
}

static void command(const char *line)
{
    if (strcmp(line, "STATUS") == 0) {
        report_rail();
    } else if (strcmp(line, "STOP") == 0) {
        const esp_err_t err = candis_motor_stop();
        printf("Motor stop: %s\n", esp_err_to_name(err));
        release_download_guard();
    } else if (strcmp(line, "PULSE 75") == 0 || strcmp(line, "PULSE 100") == 0) {
        const esp_err_t err = start_pulse(strcmp(line, "PULSE 75") == 0 ? 75 : 100);
        if (err != ESP_OK) {
            printf("MOTOR_START %s\n", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    puts("MOTOR_READY GPIO5 PWM=20000 Hz BLDO2=2700 mV commands=STATUS,PULSE 75,PULSE 100,STOP");
    report_rail();

    char line[16];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        release_download_guard();
        char next;
        if (!candis_example_usb_read(&next)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (next == '\r' || next == '\n') {
            if (length && !overflow) {
                line[length] = '\0';
                command(line);
            }
            length = 0;
            overflow = false;
        } else if (length < sizeof(line) - 1) {
            line[length++] = next;
        } else {
            overflow = true;
        }
    }
}
