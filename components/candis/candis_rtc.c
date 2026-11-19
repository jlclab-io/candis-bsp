/* SPDX-License-Identifier: Apache-2.0 */
/* Candis 1.2, RX8130CE. Epson ETM50E-10 Table 12 and sections 18.3–18.6. */
#include <string.h>
#include "bsp/candis_rtc.h"
#include "candis_internal.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum {
    RX8130_CALENDAR = 0x10,
    RX8130_EXTENSION = 0x1c,
    RX8130_FLAGS = 0x1d,
    RX8130_CONTROL = 0x1e,
    RX8130_BACKUP_CONTROL = 0x1f,
    RX8130_OFFSET = 0x30,
    RX8130_BACKUP_DETECT = 0x31,
    RX8130_FLAG_VLF = 0x02,
    RX8130_FLAG_UF = 0x20,
    RX8130_W0C_FLAGS = 0xbf,
    RX8130_TIMER_ENABLE = 0x10,
    RX8130_MINUTE_UPDATES = 0x20,
    RX8130_CONTROL_UIE = 0x20,
    RX8130_CONTROL_STOP = 0x40,
    RX8130_CONTROL_TEST_OR_IRQ = 0xb8,
    RX8130_BACKUP_INIT = 0x10,
    RX8130_BACKUP_CHARGE = 0x20,
    RX8130_TIMEOUT_MS = 100,
};

static const char *TAG = "candis_rtc";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static bool s_calendar_unconfirmed;
static const uint8_t s_month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int days_in_month(int year, int month)
{
    return s_month_days[month] + (month == 1 && year % 4 == 0);
}

static int year_day(const struct tm *utc)
{
    int days = utc->tm_mday - 1;
    for (int month = 0; month < utc->tm_mon; ++month) {
        days += days_in_month(utc->tm_year, month);
    }
    return days;
}

static int decode_bcd(uint8_t packed, int minimum, int maximum)
{
    unsigned low = packed & 0xf;
    int value = (packed >> 4) * 10 + low;
    return low <= 9 && value >= minimum && value <= maximum ? value : -1;
}

static uint8_t encode_bcd(unsigned value)
{
    return ((value / 10) << 4) | (value % 10);
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(s_rtc, &reg, 1, data, size, RX8130_TIMEOUT_MS);
}

static esp_err_t rtc_write(uint8_t reg, uint8_t value)
{
    const uint8_t packet[] = {reg, value};
    return i2c_master_transmit(s_rtc, packet, sizeof(packet), RX8130_TIMEOUT_MS);
}

static esp_err_t write_checked(uint8_t reg, uint8_t value)
{
    ESP_RETURN_ON_ERROR(rtc_write(reg, value), TAG, "write register 0x%02x", reg);
    uint8_t actual;
    ESP_RETURN_ON_ERROR(rtc_read(reg, &actual, 1), TAG, "read register 0x%02x", reg);
    return actual == value ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t read_calendar(struct tm *utc)
{
    if (s_calendar_unconfirmed) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t regs[RX8130_CONTROL + 1 - RX8130_CALENDAR];
    /* The oscillator freezes calendar carry during this burst transfer. */
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_CALENDAR, regs, sizeof(regs)), TAG, "read calendar");
    const bool voltage_low = regs[RX8130_FLAGS - RX8130_CALENDAR] & RX8130_FLAG_VLF;
    const bool stopped = regs[RX8130_CONTROL - RX8130_CALENDAR] & RX8130_CONTROL_STOP;
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
            result.tm_mon < 0 || result.tm_year < 100 || !week || (week & (week - 1)) ||
            result.tm_mday > days_in_month(result.tm_year, result.tm_mon)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    result.tm_wday = __builtin_ctz((unsigned)week);
    result.tm_yday = year_day(&result);
    *utc = result;
    return ESP_OK;
}

static esp_err_t open_rtc(void)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CANDIS_RTC_ADDR, .scl_speed_hz = 100000,
    };
    if (!s_bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_acquire_locked(&s_bus), TAG, "acquire main I2C");
    }
    if (!s_rtc) {
        return i2c_master_bus_add_device(s_bus, &config, &s_rtc);
    }
    return ESP_OK;
}

static esp_err_t close_rtc(esp_err_t result)
{
    esp_err_t cleanup = ESP_OK;
    if (s_rtc) {
        cleanup = i2c_master_bus_rm_device(s_rtc);
        if (cleanup == ESP_OK) {
            s_rtc = NULL;
        }
    }
    if (s_bus && !s_rtc) {
        cleanup = candis_i2c_release_locked(s_bus);
        if (cleanup == ESP_OK) {
            s_bus = NULL;
        }
    }
    candis_control_leave();
    return result == ESP_OK ? cleanup : result;
}

static esp_err_t configure_invalid_rtc(void)
{
    /* Candis clock-only recovery follows the tested power-switch initialization.
     * CHGEN is zero for both writes. TG28 power and shared IRQ stay untouched. */
    ESP_RETURN_ON_ERROR(write_checked(RX8130_OFFSET, 0), TAG, "disable digital offset");
    ESP_RETURN_ON_ERROR(write_checked(RX8130_BACKUP_DETECT, 0), TAG, "disable backup detection");
    ESP_RETURN_ON_ERROR(write_checked(RX8130_EXTENSION, 0x04), TAG, "set idle extension");
    ESP_RETURN_ON_ERROR(write_checked(RX8130_BACKUP_CONTROL, RX8130_BACKUP_INIT), TAG, "initialize backup switch");
    return write_checked(RX8130_BACKUP_CONTROL, 0);
}

static esp_err_t write_calendar_packet(const uint8_t *packet, size_t count)
{
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_rtc, packet, count + 1, RX8130_TIMEOUT_MS), TAG, "write calendar");
    uint8_t actual[12];
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_CALENDAR, actual, count), TAG, "verify calendar");
    return memcmp(actual, packet + 1, count) ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

static esp_err_t write_calendar(const struct tm *utc, bool initialize)
{
    unsigned yrs = utc->tm_year - 100;
    /* 2000-01-01 was Saturday. Every fourth year is leap in this range. */
    unsigned weekday = (6 + 365 * yrs + (yrs + 3) / 4 + year_day(utc)) % 7;
    const uint8_t packet[] = {
        RX8130_CALENDAR, encode_bcd(utc->tm_sec), encode_bcd(utc->tm_min), encode_bcd(utc->tm_hour),
        1U << weekday, encode_bcd(utc->tm_mday), encode_bcd(utc->tm_mon + 1), encode_bcd(yrs),
        0x80, 0x80, 0x80, 0, 0,
    };
    return write_calendar_packet(packet, initialize ? sizeof(packet) - 1 : 7);
}

static esp_err_t clear_invalid_flag(void)
{
    for (unsigned attempt = 0; attempt < 30; ++attempt) {
        uint8_t flags;
        /* Write ones to leave all other W0C flags pending. */
        ESP_RETURN_ON_ERROR(rtc_write(RX8130_FLAGS, RX8130_W0C_FLAGS & ~RX8130_FLAG_VLF), TAG, "clear VLF");
        ESP_RETURN_ON_ERROR(rtc_read(RX8130_FLAGS, &flags, 1), TAG, "read flags");
        if (!(flags & RX8130_FLAG_VLF)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t set_clock(const struct tm *utc, bool recover_invalid)
{
    uint8_t cfg[4];
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_EXTENSION, cfg, sizeof(cfg)), TAG, "read RTC configuration");
    const bool invalid = cfg[1] & RX8130_FLAG_VLF;
    const uint8_t control = cfg[2] & ~RX8130_CONTROL_STOP;
    if ((cfg[0] & RX8130_TIMER_ENABLE) || (control & RX8130_CONTROL_TEST_OR_IRQ) ||
            (invalid && (!recover_invalid || (cfg[3] & RX8130_BACKUP_CHARGE)))) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = write_checked(RX8130_CONTROL, control | RX8130_CONTROL_STOP);
    if (err == ESP_OK && invalid) {
        err = configure_invalid_rtc();
    }
    if (err == ESP_OK && invalid) {
        err = write_calendar(utc, invalid);
    } else if (err == ESP_OK) {
        uint8_t saved[8] = {RX8130_CALENDAR};
        err = rtc_read(RX8130_CALENDAR, saved + 1, sizeof(saved) - 1);
        if (err == ESP_OK) {
            err = write_calendar(utc, false);
            if (err != ESP_OK) {
                esp_err_t rollback = write_calendar_packet(saved, sizeof(saved) - 1);
                if (rollback != ESP_OK) {
                    s_calendar_unconfirmed = true;
                    ESP_LOGE(TAG, "could not confirm previous calendar: %s", esp_err_to_name(rollback));
                }
            }
        }
    }
    /* A failed write can still reach the RTC. Always attempt to restart it. */
    esp_err_t restart = write_checked(RX8130_CONTROL, control);
    if (restart != ESP_OK) {
        ESP_LOGE(TAG, "could not confirm RTC restart: %s", esp_err_to_name(restart));
        return err == ESP_OK ? restart : err;
    }
    if (err == ESP_OK && invalid) {
        err = clear_invalid_flag();
    }
    if (err == ESP_OK) {
        s_calendar_unconfirmed = false;
    }
    return err;
}

esp_err_t candis_rtc_get_time(struct tm *utc)
{
    if (!utc) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_rtc();
    if (err == ESP_OK) {
        err = read_calendar(utc);
    }
    return close_rtc(err);
}

esp_err_t candis_rtc_set_time(const struct tm *utc, bool recover_invalid)
{
    if (!utc || utc->tm_year < 100 || utc->tm_year > 199 || utc->tm_mon < 0 || utc->tm_mon > 11 ||
            utc->tm_mday < 1 || utc->tm_mday > days_in_month(utc->tm_year, utc->tm_mon) ||
            utc->tm_hour < 0 || utc->tm_hour > 23 || utc->tm_min < 0 || utc->tm_min > 59 ||
            utc->tm_sec < 0 || utc->tm_sec > 59) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = open_rtc();
    if (err == ESP_OK) {
        err = set_clock(utc, recover_invalid);
    }
    return close_rtc(err);
}

static esp_err_t restore_update_irq(candis_rtc_update_irq_t *state)
{
    uint8_t config[3];
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_EXTENSION, config, sizeof(config)), TAG, "read update IRQ configuration");
    ESP_RETURN_ON_ERROR(write_checked(RX8130_CONTROL, config[2] & ~RX8130_CONTROL_UIE), TAG, "disable update IRQ");
    ESP_RETURN_ON_ERROR(rtc_write(RX8130_FLAGS, RX8130_W0C_FLAGS & ~RX8130_FLAG_UF), TAG, "acknowledge RTC update");
    uint8_t extension = config[0] & ~RX8130_MINUTE_UPDATES;
    if (state->minute_updates) {
        extension |= RX8130_MINUTE_UPDATES;
    }
    ESP_RETURN_ON_ERROR(write_checked(RX8130_EXTENSION, extension), TAG, "restore RTC update interval");
    *state = (candis_rtc_update_irq_t){0};
    return ESP_OK;
}

static esp_err_t arm_update_irq(candis_rtc_update_irq_t *state)
{
    struct tm utc;
    ESP_RETURN_ON_ERROR(read_calendar(&utc), TAG, "validate RTC calendar");
    uint8_t config[3];
    ESP_RETURN_ON_ERROR(rtc_read(RX8130_EXTENSION, config, sizeof(config)), TAG, "read RTC configuration");
    if ((config[0] & RX8130_TIMER_ENABLE) ||
            (config[1] & RX8130_FLAG_VLF) ||
            (config[2] & (RX8130_CONTROL_STOP | RX8130_CONTROL_TEST_OR_IRQ))) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Retain the original interval before the first write. A failed write can reach the RTC. */
    *state = (candis_rtc_update_irq_t){
        .restore_pending = true, .minute_updates = !!(config[0] & RX8130_MINUTE_UPDATES),
    };
    esp_err_t result = write_checked(RX8130_EXTENSION, config[0] & ~RX8130_MINUTE_UPDATES);
    if (result == ESP_OK) {
        result = rtc_write(RX8130_FLAGS, RX8130_W0C_FLAGS & ~RX8130_FLAG_UF);
    }
    if (result == ESP_OK) {
        result = write_checked(RX8130_CONTROL, config[2] | RX8130_CONTROL_UIE);
    }
    if (result != ESP_OK) {
        esp_err_t restore = restore_update_irq(state);
        if (restore != ESP_OK) {
            ESP_LOGE(TAG, "could not restore RTC update IRQ: %s", esp_err_to_name(restore));
        }
    }
    return result;
}

esp_err_t candis_rtc_arm_update_irq(candis_rtc_update_irq_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state->restore_pending || !candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = open_rtc();
    if (result == ESP_OK) {
        result = arm_update_irq(state);
    }
    return close_rtc(result);
}

esp_err_t candis_rtc_disarm_update_irq(candis_rtc_update_irq_t *state, bool *updated)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t flags = 0;
    esp_err_t result = open_rtc();
    if (result == ESP_OK && state->restore_pending) {
        esp_err_t flag_result = updated ? rtc_read(RX8130_FLAGS, &flags, 1) : ESP_OK;
        result = restore_update_irq(state);
        if (result == ESP_OK) {
            result = flag_result;
        }
    }
    result = close_rtc(result);
    if (result == ESP_OK && updated) {
        *updated = (flags & (RX8130_FLAG_UF | RX8130_FLAG_VLF)) == RX8130_FLAG_UF;
    }
    return result;
}
