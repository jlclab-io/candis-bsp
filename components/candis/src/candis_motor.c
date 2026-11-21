/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include "bsp/candis_motor.h"
#include "bsp/candis_power.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "candis_motor";
static const ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
static const ledc_channel_t CHANNEL = LEDC_CHANNEL_0;
static const ledc_timer_t TIMER = LEDC_TIMER_0;
static _Atomic(SemaphoreHandle_t) s_lock;
static StaticSemaphore_t s_lock_storage;
static struct {
    candis_expansion_ldo_handle_t rail;
    TaskHandle_t worker;
    int64_t deadline_us;
    esp_err_t last_error;
    bool timer;
    bool channel;
    bool pin;
} s_motor;

static SemaphoreHandle_t motor_mutex(void)
{
    SemaphoreHandle_t lock = atomic_load(&s_lock);
    if (!lock && candis_control_enter()) {
        lock = atomic_load(&s_lock);
        if (!lock) {
            lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
            atomic_store(&s_lock, lock);
        }
        candis_control_leave();
    }
    return lock;
}

static esp_err_t release_motor(void)
{
    if (s_motor.channel) {
        const esp_err_t pwm = ledc_stop(MODE, CHANNEL, 0);
        if (pwm != ESP_OK) {
            /* Disconnect the FET gate and let its external pull-down hold it low. */
            if (s_motor.pin && gpio_reset_pin(BSP_MOTOR_PWM) == ESP_OK) {
                s_motor.pin = false;
                s_motor.deadline_us = 0;
            }
            if (!s_motor.pin && s_motor.rail && candis_expansion_ldo_release(s_motor.rail) == ESP_OK) {
                s_motor.rail = NULL;
            }
            return pwm;
        }
        s_motor.deadline_us = 0;
        const ledc_channel_config_t channel = {
            .speed_mode = MODE, .channel = CHANNEL, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "release motor channel");
        s_motor.channel = false;
    }
    if (s_motor.pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(BSP_MOTOR_PWM), TAG, "release GPIO5");
        s_motor.pin = false;
    }
    if (s_motor.timer) {
        ESP_RETURN_ON_ERROR(ledc_timer_pause(MODE, TIMER), TAG, "pause motor timer");
        const ledc_timer_config_t timer = {
            .speed_mode = MODE, .timer_num = TIMER, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "release motor timer");
        s_motor.timer = false;
    }
    if (s_motor.rail) {
        ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(s_motor.rail), TAG, "release motor supply");
        s_motor.rail = NULL;
    }
    return ESP_OK;
}

static bool has_resources(void)
{
    return s_motor.worker || s_motor.timer || s_motor.channel || s_motor.pin || s_motor.rail;
}

static void motor_worker(void *arg)
{
    SemaphoreHandle_t lock = arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xSemaphoreTake(lock, portMAX_DELAY);
    const int64_t remaining = s_motor.deadline_us - esp_timer_get_time();
    xSemaphoreGive(lock);
    if (remaining > 0) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS((remaining + 999) / 1000) + 1);
    }

    for (unsigned attempt = 0; ; ++attempt) {
        xSemaphoreTake(lock, portMAX_DELAY);
        esp_err_t err = release_motor();
        if (err == ESP_OK || attempt == 2) {
            if (err != ESP_OK) {
                s_motor.last_error = err;
            }
            s_motor.worker = NULL;
            xSemaphoreGive(lock);
            vTaskDelete(NULL);
            return;
        }
        xSemaphoreGive(lock);
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
    }
}

static esp_err_t start_motor(SemaphoreHandle_t lock, unsigned percent, unsigned duration_ms)
{
    candis_supply_state_t supply;
    ESP_RETURN_ON_ERROR(candis_power_read_supply(&supply), TAG, "read USB supply");
    ESP_RETURN_ON_FALSE(supply.vbus_good && supply.system_on, ESP_ERR_INVALID_STATE, TAG, "USB input required");
    const ledc_timer_config_t timer = {
        .speed_mode = MODE, .timer_num = TIMER, .freq_hz = 20000,
        .duty_resolution = LEDC_TIMER_10_BIT, .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "configure motor timer");
    s_motor.timer = true;
    const ledc_channel_config_t channel = {
        .speed_mode = MODE, .timer_sel = TIMER, .channel = CHANNEL, .gpio_num = BSP_MOTOR_PWM, .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "configure motor PWM");
    s_motor.channel = true;
    s_motor.pin = true;
    ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(CANDIS_EXPANSION_BLDO2, 2700, 20, &s_motor.rail),
                        TAG, "prepare motor supply");
    ESP_RETURN_ON_FALSE(xTaskCreate(motor_worker, "candis_motor", 3072, lock, 6, &s_motor.worker) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "allocate pulse task");
    ESP_RETURN_ON_ERROR(ledc_set_duty(MODE, CHANNEL, 1023U * percent / 100), TAG, "set motor duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(MODE, CHANNEL), TAG, "apply motor duty");
    s_motor.deadline_us = esp_timer_get_time() + duration_ms * 1000;
    return ESP_OK;
}

esp_err_t candis_motor_pulse(unsigned percent, unsigned duration_ms)
{
    if (!percent || percent > 100 || !duration_ms || duration_ms > CANDIS_MOTOR_MAX_PULSE_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = motor_mutex();
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (has_resources()) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_motor(lock, percent, duration_ms);
    if (err != ESP_OK) {
        const esp_err_t cleanup = release_motor();
        s_motor.last_error = cleanup != ESP_OK ? cleanup : err;
    } else {
        s_motor.last_error = ESP_OK;
    }
    if (s_motor.worker) {
        xTaskNotifyGive(s_motor.worker);
    }
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_stop(void)
{
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = atomic_load(&s_lock);
    if (!lock) {
        return ESP_OK;
    }
    if (xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = release_motor();
    s_motor.last_error = err;
    if (s_motor.worker) {
        xTaskNotifyGive(s_motor.worker);
    }
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_get_state(candis_motor_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = atomic_load(&s_lock);
    if (!lock) {
        *state = (candis_motor_state_t){0};
        return ESP_OK;
    }
    if (xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    *state = (candis_motor_state_t){
        .active = s_motor.deadline_us != 0,
        .cleanup_pending = has_resources() && !s_motor.deadline_us,
        .last_error = s_motor.last_error,
    };
    xSemaphoreGive(lock);
    return ESP_OK;
}
