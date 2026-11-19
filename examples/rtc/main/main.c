/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bsp/candis_rtc.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"

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
        puts("RTC_COMMAND expected READ, SET <UTC epoch>, or RECOVER <UTC epoch>");
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
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
    puts("RTC_READY commands=READ,SET,RECOVER utc=epoch_seconds");
    report_time();
    char line[48];
    size_t count = 0;
    bool overflow = false;
    for (;;) {
        char next;
        if (usb_serial_jtag_read_bytes(&next, 1, portMAX_DELAY) != 1) {
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
