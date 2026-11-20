/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bsp/candis_rtc.h"

esp_err_t candis_rtc_example_sleep(void);
void candis_rtc_example_resume(void);
void candis_rtc_example_report_wake(void);

static void report_time(void)
{
    struct tm time;
    esp_err_t result = candis_rtc_get_time(&time);
    if (result != ESP_OK) {
        printf("RTC_READ %s\n", esp_err_to_name(result));
        return;
    }
    char text[24];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &time);
    printf("RTC_READ ESP_OK %s weekday=%d\n", text, time.tm_wday);
}

static void handle_command(const char *command)
{
    if (strcmp(command, "READ") == 0) {
        report_time();
        return;
    }
    if (strcmp(command, "SLEEP") == 0) {
        if (!candis_example_usb_begin_operation()) {
            puts("RTC_SLEEP USB download in progress");
            return;
        }
        const esp_err_t result = candis_rtc_example_sleep();
        candis_example_usb_end_operation();
        printf("RTC_SLEEP %s\n", esp_err_to_name(result));
        return;
    }
    if (strcmp(command, "WAKE") == 0) {
        candis_rtc_example_report_wake();
        return;
    }
    const bool recover = strncmp(command, "RECOVER ", 8) == 0;
    const char *argument = NULL;
    if (recover) {
        argument = command + 8;
    } else if (strncmp(command, "SET ", 4) == 0) {
        argument = command + 4;
    }
    char *end = NULL;
    const int64_t seconds = argument && *argument >= '0' && *argument <= '9' ? strtoll(argument, &end, 10) : 0;
    if (!end || *end != '\0' || seconds < INT64_C(946684800) || seconds >= INT64_C(4102444800)) {
        puts("RTC_COMMAND expected READ, SET <UTC epoch>, RECOVER <UTC epoch>, SLEEP, or WAKE");
        return;
    }
    const time_t epoch = seconds;
    struct tm time;
    if (!gmtime_r(&epoch, &time)) {
        puts("RTC_SET ESP_ERR_INVALID_ARG");
        return;
    }
    const esp_err_t result = candis_rtc_set_time(&time, recover);
    printf("RTC_SET %s\n", esp_err_to_name(result));
    if (result == ESP_OK) {
        report_time();
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    candis_rtc_example_resume();
    puts("RTC_READY commands=READ,SET,RECOVER,SLEEP,WAKE utc=epoch_seconds");
    report_time();
    char line[48];
    size_t count = 0;
    bool overflow = false;
    for (;;) {
        char next;
        if (!candis_example_usb_read(&next)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (next == '\n' || next == '\r') {
            if (count && !overflow) {
                line[count] = '\0';
                handle_command(line);
            }
            count = 0;
            overflow = false;
        } else if (count < sizeof(line) - 1) {
            line[count++] = next;
        } else {
            overflow = true;
        }
    }
}
