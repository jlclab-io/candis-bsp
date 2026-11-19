/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2. RX8130CE on the main I2C bus. */
#include "bsp/candis_rtc.h"
#include "candis_internal.h"
#include "esp_check.h"

#define RX8130_ADDRESS 0x32
#define RX8130_CALENDAR 0x10
#define RX8130_FLAGS 0x1d
#define RX8130_FLAG_VLF 0x02
#define RX8130_CONTROL_STOP 0x40
#define RX8130_TIMEOUT_MS 100

static const char *TAG = "candis_rtc";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;

static int decode_bcd(uint8_t packed, int minimum, int maximum)
{
    unsigned low = packed & 0xf;
    int value = (packed >> 4) * 10 + low;
    return low <= 9 && value >= minimum && value <= maximum ? value : -1;
}

static esp_err_t read_calendar(struct tm *utc)
{
    const uint8_t address = RX8130_CALENDAR;
    uint8_t regs[RX8130_FLAGS + 2 - RX8130_CALENDAR];
    /* RX8130CE freezes calendar carry for the duration of this burst transfer. */
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_rtc, &address, 1, regs, sizeof(regs), RX8130_TIMEOUT_MS),
                        TAG, "read calendar");
    const bool voltage_low = regs[RX8130_FLAGS - RX8130_CALENDAR] & RX8130_FLAG_VLF;
    const bool stopped = regs[RX8130_FLAGS + 1 - RX8130_CALENDAR] & RX8130_CONTROL_STOP;
    if (voltage_low || stopped) {
        ESP_LOGW(TAG, "RTC calendar validity: VLF=%d STOP=%d", voltage_low, stopped);
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t week = regs[3] & 0x7f;
    struct tm result = {
        .tm_sec = decode_bcd(regs[0] & 0x7f, 0, 59),
        .tm_min = decode_bcd(regs[1] & 0x7f, 0, 59),
        .tm_hour = decode_bcd(regs[2] & 0x3f, 0, 23),
        .tm_mday = decode_bcd(regs[4] & 0x3f, 1, 31),
        .tm_mon = decode_bcd(regs[5] & 0x1f, 1, 12) - 1,
        .tm_year = decode_bcd(regs[6], 0, 99) + 100,
    };
    if (result.tm_sec < 0 || result.tm_min < 0 || result.tm_hour < 0 || result.tm_mday < 0 ||
        result.tm_mon < 0 || result.tm_year < 100 || !week || (week & (week - 1))) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month_days = days[result.tm_mon];
    if (result.tm_mon == 1 && result.tm_year % 4 == 0) {
        ++month_days;
    }
    if (result.tm_mday > month_days) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result.tm_wday = __builtin_ctz((unsigned)week);
    result.tm_yday = result.tm_mday - 1;
    for (int month = 0; month < result.tm_mon; ++month) {
        result.tm_yday += days[month];
    }
    if (result.tm_mon > 1 && result.tm_year % 4 == 0) {
        ++result.tm_yday;
    }
    *utc = result;
    return ESP_OK;
}

esp_err_t candis_rtc_get_time(struct tm *utc)
{
    if (!utc) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RX8130_ADDRESS, .scl_speed_hz = 100000,
    };
    esp_err_t err = ESP_OK;
    if (!s_bus) {
        err = candis_i2c_acquire_locked(&s_bus);
    }
    if (err == ESP_OK && !s_rtc) {
        err = i2c_master_bus_add_device(s_bus, &device, &s_rtc);
    }
    if (err == ESP_OK) {
        err = read_calendar(utc);
    }
    if (s_rtc) {
        esp_err_t cleanup = i2c_master_bus_rm_device(s_rtc);
        if (cleanup == ESP_OK) {
            s_rtc = NULL;
        } else if (err == ESP_OK) {
            err = cleanup;
        }
    }
    if (s_bus && !s_rtc) {
        esp_err_t cleanup = candis_i2c_release_locked(s_bus);
        if (cleanup == ESP_OK) {
            s_bus = NULL;
        } else if (err == ESP_OK) {
            err = cleanup;
        }
    }
    candis_control_leave();
    return err;
}
