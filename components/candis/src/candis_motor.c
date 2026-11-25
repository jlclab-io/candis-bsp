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
static _Atomic(SemaphoreHandle_t) s_lock;
static StaticSemaphore_t s_lock_storage;
static candis_motor_config_t s_config;
static bool s_configured;
static struct {
    candis_expansion_ldo_handle_t rail;
    TaskHandle_t worker;
    int64_t deadline_us;
    esp_err_t last_error;
    bool timer;
    bool channel;
    bool pin;
    bool running;
    uint8_t amplitude_percent;
    uint8_t duty_percent;
    uint32_t frequency_hz;
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
    s_motor.running = false;
    s_motor.deadline_us = 0;
    if (s_motor.channel) {
        const esp_err_t pwm = ledc_stop(s_config.speed_mode, s_config.channel, 0);
        if (pwm != ESP_OK) {
            /* Disconnect the FET gate and let its external pull-down hold it low. */
            if (s_motor.pin && gpio_reset_pin(s_config.pwm_gpio) == ESP_OK) {
                s_motor.pin = false;
                s_motor.deadline_us = 0;
            }
            if (!s_motor.pin && s_motor.rail && candis_expansion_ldo_release(s_motor.rail) == ESP_OK) {
                s_motor.rail = CANDIS_EXPANSION_LDO_HANDLE_NONE;
            }
            return pwm;
        }
        s_motor.deadline_us = 0;
        const ledc_channel_config_t channel = {
            .speed_mode = s_config.speed_mode, .channel = s_config.channel, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "release motor channel");
        s_motor.channel = false;
    }
    if (s_motor.pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(s_config.pwm_gpio), TAG, "release motor GPIO");
        s_motor.pin = false;
    }
    if (s_motor.timer) {
        ESP_RETURN_ON_ERROR(ledc_timer_pause(s_config.speed_mode, s_config.timer), TAG, "pause motor timer");
        const ledc_timer_config_t timer = {
            .speed_mode = s_config.speed_mode, .timer_num = s_config.timer, .deconfigure = true,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "release motor timer");
        s_motor.timer = false;
    }
    if (s_motor.rail) {
        ESP_RETURN_ON_ERROR(candis_expansion_ldo_release(s_motor.rail), TAG, "release motor supply");
        s_motor.rail = CANDIS_EXPANSION_LDO_HANDLE_NONE;
    }
    return ESP_OK;
}

static bool has_resources(void)
{
    return s_motor.worker || s_motor.timer || s_motor.channel || s_motor.pin || s_motor.rail;
}

static uint32_t duty_value(unsigned percent)
{
    return ((1U << s_config.duty_resolution) - 1U) * percent / 100U;
}

static esp_err_t set_duty(unsigned percent)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(s_config.speed_mode, s_config.channel, duty_value(percent)),
                        TAG, "set motor duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(s_config.speed_mode, s_config.channel),
                        TAG, "apply motor duty");
    s_motor.duty_percent = percent;
    s_motor.amplitude_percent = candis_motor_duty_to_amplitude(percent);
    return ESP_OK;
}

static bool valid_config(const candis_motor_config_t *config)
{
    if (!config || config->pwm_gpio == GPIO_NUM_NC || !config->frequency_hz ||
        config->duty_resolution == 0 || config->amplitude_min_duty_percent == 0 ||
        config->amplitude_min_duty_percent > config->amplitude_max_duty_percent ||
        config->amplitude_max_duty_percent > 100) {
        return false;
    }
    if (!config->use_ldo) {
        return true;
    }
    if (config->supply_mv < 500 || config->supply_mv > 3500 || config->supply_mv % 100) {
        return false;
    }
    switch (config->supply_rail) {
    case CANDIS_EXPANSION_ALDO3:
    case CANDIS_EXPANSION_ALDO4:
    case CANDIS_EXPANSION_BLDO1:
    case CANDIS_EXPANSION_BLDO2:
        return true;
    default:
        return false;
    }
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

static esp_err_t start_motor(unsigned percent)
{
    if (s_config.require_usb) {
        candis_supply_state_t supply;
        ESP_RETURN_ON_ERROR(candis_power_read_supply(&supply), TAG, "read USB supply");
        ESP_RETURN_ON_FALSE(supply.vbus_good && supply.system_on, ESP_ERR_INVALID_STATE, TAG, "USB input required");
    }
    const ledc_timer_config_t timer = {
        .speed_mode = s_config.speed_mode, .timer_num = s_config.timer,
        .freq_hz = s_config.frequency_hz, .duty_resolution = s_config.duty_resolution,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "configure motor timer");
    s_motor.timer = true;
    const ledc_channel_config_t channel = {
        .speed_mode = s_config.speed_mode, .timer_sel = s_config.timer,
        .channel = s_config.channel, .gpio_num = s_config.pwm_gpio, .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "configure motor PWM");
    s_motor.channel = true;
    s_motor.pin = true;
    if (s_config.use_ldo) {
        ESP_RETURN_ON_ERROR(candis_expansion_ldo_acquire(s_config.supply_rail, s_config.supply_mv,
                                                         s_config.supply_settle_ms, &s_motor.rail),
                            TAG, "prepare motor supply");
    }
    ESP_RETURN_ON_ERROR(set_duty(percent), TAG, "apply motor duty");
    s_motor.running = true;
    s_motor.frequency_hz = s_config.frequency_hz;
    return ESP_OK;
}

uint8_t candis_motor_amplitude_to_duty(uint8_t amplitude_percent)
{
    if (!s_configured || !amplitude_percent) {
        return 0;
    }
    if (amplitude_percent > 100) {
        amplitude_percent = 100;
    }
    const unsigned minimum = s_config.amplitude_min_duty_percent;
    const unsigned maximum = s_config.amplitude_max_duty_percent;
    return minimum + ((maximum - minimum) * (amplitude_percent - 1U) + 49U) / 99U;
}

uint8_t candis_motor_duty_to_amplitude(uint8_t duty_percent)
{
    if (!s_configured || !duty_percent) {
        return 0;
    }
    const unsigned minimum = s_config.amplitude_min_duty_percent;
    const unsigned maximum = s_config.amplitude_max_duty_percent;
    if (duty_percent <= minimum) {
        return 1;
    }
    if (duty_percent >= maximum) {
        return 100;
    }
    return 1U + ((unsigned)(duty_percent - minimum) * 99U + (maximum - minimum) / 2U) /
                    (maximum - minimum);
}

esp_err_t candis_motor_init(const candis_motor_config_t *config)
{
    if (!valid_config(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    SemaphoreHandle_t lock = motor_mutex();
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = has_resources() ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (err == ESP_OK) {
        s_config = *config;
        s_configured = true;
    }
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_pulse(unsigned percent, unsigned duration_ms)
{
    if (!percent || percent > 100 || !duration_ms || duration_ms > CANDIS_MOTOR_MAX_PULSE_MS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_configured) {
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
    esp_err_t err = start_motor(percent);
    if (err == ESP_OK &&
        xTaskCreate(motor_worker, "candis_motor", 3072, lock, 6, &s_motor.worker) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        s_motor.deadline_us = esp_timer_get_time() + duration_ms * 1000;
    }
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

esp_err_t candis_motor_start(unsigned percent)
{
    if (!percent || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext() || !s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = motor_mutex();
    if (!lock || xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = has_resources() ? ESP_ERR_INVALID_STATE : start_motor(percent);
    if (err != ESP_OK) {
        const esp_err_t cleanup = release_motor();
        s_motor.last_error = cleanup != ESP_OK ? cleanup : err;
    } else {
        s_motor.last_error = ESP_OK;
    }
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_set_duty(unsigned percent)
{
    if (!percent || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = atomic_load(&s_lock);
    if (!lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_motor.running && s_motor.channel ? set_duty(percent) : ESP_ERR_INVALID_STATE;
    s_motor.last_error = err;
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_start_amplitude(uint8_t amplitude_percent)
{
    if (!amplitude_percent || amplitude_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t duty = candis_motor_amplitude_to_duty(amplitude_percent);
    return duty ? candis_motor_start(duty) : ESP_ERR_INVALID_STATE;
}

esp_err_t candis_motor_set_amplitude(uint8_t amplitude_percent)
{
    if (amplitude_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!amplitude_percent) {
        return candis_motor_stop();
    }
    const uint8_t duty = candis_motor_amplitude_to_duty(amplitude_percent);
    return duty ? candis_motor_set_duty(duty) : ESP_ERR_INVALID_STATE;
}

esp_err_t candis_motor_set_frequency(uint32_t frequency_hz)
{
    if (!frequency_hz) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xPortInIsrContext() || !s_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    SemaphoreHandle_t lock = atomic_load(&s_lock);
    if (!lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(lock, 0) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!has_resources()) {
        s_config.frequency_hz = frequency_hz;
        s_motor.frequency_hz = frequency_hz;
        err = ESP_OK;
    } else if (s_motor.running && s_motor.timer) {
        const ledc_timer_config_t timer = {
            .speed_mode = s_config.speed_mode,
            .timer_num = s_config.timer,
            .freq_hz = frequency_hz,
            .duty_resolution = s_config.duty_resolution,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        err = ledc_timer_config(&timer);
        if (err == ESP_OK) {
            err = set_duty(s_motor.duty_percent);
        }
        if (err == ESP_OK) {
            s_config.frequency_hz = frequency_hz;
            s_motor.frequency_hz = frequency_hz;
        }
    }
    s_motor.last_error = err;
    xSemaphoreGive(lock);
    return err;
}

esp_err_t candis_motor_pulse_amplitude(uint8_t amplitude_percent, unsigned duration_ms)
{
    if (!amplitude_percent || amplitude_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t duty = candis_motor_amplitude_to_duty(amplitude_percent);
    return duty ? candis_motor_pulse(duty, duration_ms) : ESP_ERR_INVALID_STATE;
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
        .active = s_motor.running,
        .cleanup_pending = has_resources() && !s_motor.running,
        .amplitude_percent = s_motor.amplitude_percent,
        .duty_percent = s_motor.duty_percent,
        .frequency_hz = s_motor.frequency_hz,
        .last_error = s_motor.last_error,
    };
    xSemaphoreGive(lock);
    return ESP_OK;
}
