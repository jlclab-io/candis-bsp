/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "bsp/candis.h"
#include "bsp/candis_power.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "tg28_esp.h"

static const char *TAG = "candis_motor";
static const ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
static const ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
static const ledc_timer_t TIMER = LEDC_TIMER_0;

static tg28_esp_t s_pmu;
static candis_expansion_ldo_handle_t s_rail;
static int64_t s_deadline;

static esp_err_t configure_pwm(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = MODE, .timer_num = TIMER, .freq_hz = 20000,
        .duty_resolution = LEDC_TIMER_10_BIT, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "configure PWM timer");
    const ledc_channel_config_t channel = {
        .speed_mode = MODE, .timer_sel = TIMER, .channel = CHANNEL, .gpio_num = GPIO_NUM_5, .duty = 0,
    };
    return ledc_channel_config(&channel);
}

static void report_rail(void)
{
    bool enabled;
    uint16_t mv;
    esp_err_t err = tg28_esp_read_rail_enabled(&s_pmu, TG28_ESP_RAIL_BLDO2, &enabled);
    if (err == ESP_OK) {
        err = tg28_esp_read_rail_voltage(&s_pmu, TG28_ESP_RAIL_BLDO2, &mv);
    }
    if (err == ESP_OK) {
        printf("MOTOR_RAIL enabled=%u setpoint=%u mV active=%u\n", enabled, mv, s_deadline != 0);
    } else {
        printf("MOTOR_RAIL %s\n", esp_err_to_name(err));
    }
}

static void stop_motor(const char *reason)
{
    s_deadline = 0;
    const esp_err_t pwm = ledc_stop(MODE, CHANNEL, 0);
    esp_err_t rail = ESP_OK;
    if (s_rail) {
        rail = candis_expansion_ldo_release(s_rail);
        if (rail == ESP_OK) {
            s_rail = NULL;
        }
    }
    if (pwm == ESP_OK && rail == ESP_OK) {
        candis_example_usb_end_operation();
    }
    printf("MOTOR_STOP reason=%s pwm=%s release=%s\n", reason, esp_err_to_name(pwm), esp_err_to_name(rail));
    report_rail();
}

static esp_err_t start_pulse(unsigned percent)
{
    ESP_RETURN_ON_FALSE(!s_rail, ESP_ERR_INVALID_STATE, TAG, "stop or restore the previous pulse first");
    tg28_esp_status_t status;
    ESP_RETURN_ON_ERROR(tg28_esp_read_status(&s_pmu, &status), TAG, "read VBUS status");
    ESP_RETURN_ON_FALSE(status.vbus_good && status.system_on, ESP_ERR_INVALID_STATE, TAG, "USB power required");
    ESP_RETURN_ON_FALSE(candis_example_usb_begin_operation(), ESP_ERR_INVALID_STATE, TAG, "USB download in progress");
    const uint32_t duty = (1023U * percent) / 100;
    esp_err_t err = candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO2, 2700, 20, &s_rail);
    if (err == ESP_OK) {
        err = ledc_set_duty(MODE, CHANNEL, duty);
    }
    if (err == ESP_OK) {
        err = ledc_update_duty(MODE, CHANNEL);
    }
    if (err == ESP_OK) {
        s_deadline = esp_timer_get_time() + 600000;
        /* LEDC applies a duty update on the next PWM cycle. */
        vTaskDelay(pdMS_TO_TICKS(1) + 1);
        const uint32_t actual = ledc_get_duty(MODE, CHANNEL);
        if (actual != duty) {
            ESP_LOGE(TAG, "PWM duty readback expected=%lu actual=%lu", (unsigned long)duty, (unsigned long)actual);
            stop_motor("duty readback failed");
            return ESP_ERR_INVALID_RESPONSE;
        }
        printf("MOTOR_PULSE duty=%u%% steps=%lu limit_ms=600\n", percent, (unsigned long)actual);
        report_rail();
    } else {
        stop_motor("start failed");
    }
    return err;
}

static void command(const char *line)
{
    if (strcmp(line, "STATUS") == 0) {
        report_rail();
    } else if (strcmp(line, "STOP") == 0) {
        stop_motor("command");
    } else if (strcmp(line, "PULSE 75") == 0 || strcmp(line, "PULSE 100") == 0) {
        const esp_err_t err = start_pulse(strcmp(line, "PULSE 75") == 0 ? 75 : 100);
        if (err != ESP_OK) {
            printf("MOTOR_START %s\n", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(configure_pwm());
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(candis_i2c_acquire(&bus));
    const tg28_esp_config_t pmu = TG28_ESP_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(tg28_esp_open(&s_pmu, bus, &pmu));
    ESP_ERROR_CHECK(tg28_esp_check_chip_id(&s_pmu));
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    puts("MOTOR_READY GPIO5 PWM=20000 Hz BLDO2=2700 mV commands=STATUS,PULSE 75,PULSE 100,STOP");
    report_rail();

    char line[16];
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        if (s_deadline && esp_timer_get_time() >= s_deadline) {
            stop_motor("timeout");
        }
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
