/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include "bsp/candis_power.h"
#include "candis_internal.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "candis_power";
typedef struct {
    candis_power_t power;
    unsigned users;
    bool cleanup_pending;
} shared_rail_t;

static shared_rail_t s_expansion = {.power.rail = CANDIS_RAIL_EXPANSION};
static shared_rail_t s_display = {.power.rail = CANDIS_RAIL_DISPLAY};
static candis_power_t s_application_expansion = {.rail = CANDIS_RAIL_EXPANSION};
static candis_power_t s_observer;
static candis_power_t s_charge_setup;
static bool s_default_charge_applied;
static unsigned s_default_charge_init_attempts;

struct candis_expansion_power_lease {
    candis_power_t power;
};
static const tg28_esp_ldo_t s_ldo[] = {
    [CANDIS_RAIL_DISPLAY] = TG28_ESP_ALDO1,
    [CANDIS_RAIL_AUDIO] = TG28_ESP_ALDO2,
    [CANDIS_RAIL_ALDO3] = TG28_ESP_ALDO3,
    [CANDIS_RAIL_ALDO4] = TG28_ESP_ALDO4,
    [CANDIS_RAIL_BLDO1] = TG28_ESP_BLDO1,
    [CANDIS_RAIL_BLDO2] = TG28_ESP_BLDO2,
};

static struct expansion_ldo {
    candis_power_t power;
    uintptr_t generation;
} s_expansion_ldos[] = {
    [CANDIS_EXPANSION_ALDO3] = {.power.rail = CANDIS_RAIL_ALDO3},
    [CANDIS_EXPANSION_ALDO4] = {.power.rail = CANDIS_RAIL_ALDO4},
    [CANDIS_EXPANSION_BLDO1] = {.power.rail = CANDIS_RAIL_BLDO1},
    [CANDIS_EXPANSION_BLDO2] = {.power.rail = CANDIS_RAIL_BLDO2},
};
static uintptr_t s_last_ldo_generation;

static uint8_t voltage_code(uint16_t millivolts)
{
    return (millivolts - TG28_ESP_LDO_MIN_MV) / TG28_ESP_LDO_STEP_MV;
}

static esp_err_t open_pmu(candis_power_t *lease)
{
    const tg28_esp_config_t config = {
        .address = CANDIS_PMU_ADDR, .scl_speed_hz = 100000, .timeout_ms = 100,
    };
    ESP_RETURN_ON_ERROR(candis_i2c_acquire_locked(&lease->bus), TAG, "acquire main I2C");
    return tg28_esp_open(&lease->pmu, lease->bus, &config);
}

static esp_err_t acquire_rail(candis_power_t *lease, bool manage, unsigned settle_ms, uint8_t target)
{
    /* The caller holds the board control lock for all PMU register updates. */
    ESP_RETURN_ON_ERROR(open_pmu(lease), TAG, "open TG28 device");
    uint8_t enabled;
    esp_err_t err = tg28_esp_read_outputs(&lease->pmu, &enabled);
    if (err == ESP_ERR_INVALID_RESPONSE) {
        /* The first status read can NACK immediately after boot. No writes have run. */
        vTaskDelay(pdMS_TO_TICKS(5) + 1);
        err = tg28_esp_read_outputs(&lease->pmu, &enabled);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "read rail state");
    const unsigned index = lease->rail;
    const tg28_esp_ldo_t ldo = index < sizeof(s_ldo) / sizeof(s_ldo[0]) ? s_ldo[index] : 0;
    uint8_t voltage = target;
    if (ldo) {
        ESP_RETURN_ON_ERROR(tg28_esp_read_ldo_code(&lease->pmu, ldo, &voltage), TAG, "read rail voltage");
    }
    if (enabled & lease->rail) {
        return voltage == target ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_FALSE(manage, ESP_ERR_INVALID_STATE, TAG, "requested rail is off");
    ESP_RETURN_ON_FALSE(lease->rail != CANDIS_RAIL_EXPANSION || !(enabled & CANDIS_RAIL_ALDO3),
                        ESP_ERR_INVALID_STATE, TAG, "DC1SW is off while ALDO3 is on");
    if (ldo && voltage != target) {
        lease->changed_ldo = ldo;
        lease->original_voltage = voltage;
        ESP_RETURN_ON_ERROR(tg28_esp_set_ldo_code(&lease->pmu, ldo, target),
                            TAG, "set rail voltage");
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
    }
    /* Retain ownership even if a bus error occurs after sending the enable write. */
    lease->owned = true;
    ESP_RETURN_ON_ERROR(tg28_esp_set_outputs(&lease->pmu, lease->rail, true), TAG, "enable rail");
    vTaskDelay(pdMS_TO_TICKS(settle_ms) + 1);
    ESP_RETURN_ON_ERROR(tg28_esp_read_outputs(&lease->pmu, &enabled), TAG, "verify rail state");
    if (ldo) {
        ESP_RETURN_ON_ERROR(tg28_esp_read_ldo_code(&lease->pmu, ldo, &voltage), TAG, "verify rail voltage");
    }
    return (enabled & lease->rail) && voltage == target
           ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t release_rail(candis_power_t *lease)
{
    if (lease->owned) {
        if (lease->rail == CANDIS_RAIL_EXPANSION) {
            uint8_t enabled;
            ESP_RETURN_ON_ERROR(tg28_esp_read_outputs(&lease->pmu, &enabled), TAG, "read DC1SW and ALDO3");
            ESP_RETURN_ON_FALSE(!(enabled & CANDIS_RAIL_ALDO3), ESP_ERR_INVALID_STATE, TAG, "ALDO3 is still on");
        }
        ESP_RETURN_ON_ERROR(tg28_esp_set_outputs(&lease->pmu, lease->rail, false), TAG, "disable rail");
        lease->owned = false;
    }
    if (lease->changed_ldo) {
        ESP_RETURN_ON_ERROR(tg28_esp_set_ldo_code(&lease->pmu, lease->changed_ldo, lease->original_voltage),
                            TAG, "restore rail voltage");
        lease->changed_ldo = 0;
    }
    ESP_RETURN_ON_ERROR(tg28_esp_close(&lease->pmu), TAG, "remove TG28 device");
    if (lease->bus) {
        ESP_RETURN_ON_ERROR(candis_i2c_release_locked(lease->bus), TAG, "release main I2C");
        lease->bus = NULL;
    }
    return ESP_OK;
}

static shared_rail_t *shared_rail(candis_rail_t rail)
{
    switch (rail) {
    case CANDIS_RAIL_EXPANSION:
        return &s_expansion;
    case CANDIS_RAIL_DISPLAY:
        return &s_display;
    default:
        return NULL;
    }
}

esp_err_t candis_power_acquire(candis_power_t *lease, bool manage, unsigned settle_ms)
{
    shared_rail_t *shared = shared_rail(lease->rail);
    if (lease->bus || (shared && shared->cleanup_pending)) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t target = lease->rail == CANDIS_RAIL_EXPANSION ? 0 : voltage_code(3300);
    if (!shared) {
        return acquire_rail(lease, manage, settle_ms, target);
    }
    esp_err_t result = ESP_OK;
    if (shared->users == 0) {
        result = acquire_rail(&shared->power, manage, settle_ms, target);
        if (!shared->power.bus) {
            return result;
        }
        /* The first client also holds any partially acquired resources. */
        shared->cleanup_pending = result != ESP_OK;
    }
    ++shared->users;
    lease->bus = shared->power.bus;
    return result;
}

esp_err_t candis_power_release(candis_power_t *lease)
{
    shared_rail_t *shared = shared_rail(lease->rail);
    if (!shared) {
        return release_rail(lease);
    }
    if (!lease->bus) {
        return ESP_OK;
    }
    if (shared->users == 1) {
        shared->cleanup_pending = true;
        ESP_RETURN_ON_ERROR(release_rail(&shared->power), TAG, "release last shared rail lease");
        shared->cleanup_pending = false;
    }
    --shared->users;
    lease->bus = NULL;
    return ESP_OK;
}

esp_err_t candis_expansion_power_acquire(bool manage_power)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = candis_power_acquire(&s_application_expansion, manage_power, 20);
    candis_control_leave();
    return err;
}

esp_err_t candis_expansion_power_release(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = candis_power_release(&s_application_expansion);
    candis_control_leave();
    return err;
}

esp_err_t candis_expansion_power_claim(bool manage_power, candis_expansion_power_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (*handle) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    candis_expansion_power_handle_t lease = calloc(1, sizeof(*lease));
    esp_err_t err = ESP_ERR_NO_MEM;
    if (lease) {
        lease->power.rail = CANDIS_RAIL_EXPANSION;
        err = candis_power_acquire(&lease->power, manage_power, 20);
        if (err != ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(candis_power_release(&lease->power));
        }
        if (lease->power.bus) {
            *handle = lease;
        } else {
            free(lease);
        }
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_expansion_power_unclaim(candis_expansion_power_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (*handle) {
        err = candis_power_release(&(*handle)->power);
        if (err == ESP_OK) {
            free(*handle);
            *handle = NULL;
        }
    }
    candis_control_leave();
    return err;
}

static struct expansion_ldo *find_ldo_lease(uintptr_t generation)
{
    for (size_t i = 0; i < sizeof(s_expansion_ldos) / sizeof(s_expansion_ldos[0]); ++i) {
        if (s_expansion_ldos[i].generation == generation) {
            return &s_expansion_ldos[i];
        }
    }
    return NULL;
}

esp_err_t candis_expansion_ldo_acquire(candis_expansion_ldo_t rail, uint16_t voltage_mv, unsigned settle_ms,
                                      candis_expansion_ldo_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((unsigned)rail >= sizeof(s_expansion_ldos) / sizeof(s_expansion_ldos[0]) ||
        voltage_mv < TG28_ESP_LDO_MIN_MV || voltage_mv > TG28_ESP_LDO_MAX_MV ||
        (voltage_mv - TG28_ESP_LDO_MIN_MV) % TG28_ESP_LDO_STEP_MV) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const uintptr_t previous = *handle;
    if (previous && (previous > s_last_ldo_generation || find_ldo_lease(previous))) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    *handle = CANDIS_EXPANSION_LDO_HANDLE_NONE;
    struct expansion_ldo *lease = &s_expansion_ldos[rail];
    if (lease->generation || s_last_ldo_generation == UINTPTR_MAX) {
        const esp_err_t err = lease->generation ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
        candis_control_leave();
        return err;
    }
    const esp_err_t err = acquire_rail(&lease->power, true, settle_ms, voltage_code(voltage_mv));
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_power_release(&lease->power));
    }
    if (lease->power.bus) {
        /* Include leases whose acquisition failed and still need cleanup. */
        lease->generation = ++s_last_ldo_generation;
        *handle = lease->generation;
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_expansion_ldo_release(candis_expansion_ldo_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    struct expansion_ldo *lease = find_ldo_lease(handle);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (lease) {
        err = candis_power_release(&lease->power);
        if (err == ESP_OK) {
            lease->generation = CANDIS_EXPANSION_LDO_HANDLE_NONE;
        }
    }
    candis_control_leave();
    return err;
}

/* All observers share a retryable, read-only I2C lease. */
static esp_err_t observe_open(void)
{
    ESP_RETURN_ON_ERROR(candis_power_release(&s_observer), TAG, "release previous status read");
    return open_pmu(&s_observer);
}

static esp_err_t observe_finish(esp_err_t result)
{
    const esp_err_t cleanup = candis_power_release(&s_observer);
    candis_control_leave();
    return result == ESP_OK ? cleanup : result;
}

esp_err_t candis_power_read(candis_power_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t enabled = 0;
    esp_err_t err = observe_open();
    if (err == ESP_OK) {
        err = tg28_esp_read_outputs(&s_observer.pmu, &enabled);
    }
    err = observe_finish(err);
    if (err == ESP_OK) {
        *state = (candis_power_state_t){
            .panel = !!(enabled & CANDIS_RAIL_DISPLAY),
            .audio = !!(enabled & CANDIS_RAIL_AUDIO),
            .aldo3 = !!(enabled & CANDIS_RAIL_ALDO3),
            .dc1sw = !!(enabled & CANDIS_RAIL_EXPANSION),
        };
    }
    return err;
}

esp_err_t candis_power_read_supply(candis_supply_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    tg28_esp_status_t status = {0};
    candis_supply_state_t result = {0};
    esp_err_t err = observe_open();
    if (err == ESP_OK) {
        err = tg28_esp_read_status(&s_observer.pmu, &status);
    }
    if (err == ESP_OK) {
        err = tg28_esp_read_rail_enabled(&s_observer.pmu, TG28_ESP_RAIL_BLDO2, &result.bldo2_on);
    }
    if (err == ESP_OK) {
        err = tg28_esp_read_rail_voltage(&s_observer.pmu, TG28_ESP_RAIL_BLDO2, &result.bldo2_mv);
    }
    err = observe_finish(err);
    if (err == ESP_OK) {
        result.vbus_good = status.vbus_good;
        result.system_on = status.system_on;
        result.battery_present = status.battery_present;
        result.battery_charging = status.direction == TG28_ESP_BATTERY_CHARGING;
        *state = result;
    }
    return err;
}

esp_err_t candis_power_read_battery_uv(uint32_t *microvolts)
{
    if (!microvolts) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    tg28_esp_status_t status = {0};
    uint32_t value = 0;
    esp_err_t err = observe_open();
    if (err == ESP_OK) {
        err = tg28_esp_read_status(&s_observer.pmu, &status);
    }
    if (err == ESP_OK) {
        err = status.battery_present ? tg28_esp_read_adc_uv(&s_observer.pmu, TG28_ESP_ADC_VBAT, &value)
                                     : ESP_ERR_NOT_FOUND;
    }
    err = observe_finish(err);
    if (err == ESP_OK) {
        *microvolts = value;
    }
    return err;
}

static bool charge_profile_ready(const tg28_esp_status_t *status, const tg28_esp_charger_t *charger,
                                 const tg28_esp_irq_t *irq)
{
    return status->vbus_good && status->battery_present && !status->thermal_regulation &&
           !status->input_current_limit && !status->input_voltage_limit &&
           status->charge_state != TG28_ESP_CHARGE_STATE_UNKNOWN && charger->enabled &&
           charger->detection_enabled && charger->precharge_timer_enabled &&
           charger->charge_timer_enabled && charger->voltage_mv == 4200 &&
           !(irq->bank[0] & 0x0f) && !(irq->bank[2] & 0x07);
}

static esp_err_t ensure_default_charge_locked(void)
{
    const uint16_t fallback_ma = CANDIS_OTP_CHARGE_CURRENT_MA;
    const uint16_t target_ma = CANDIS_DEFAULT_CHARGE_CURRENT_MA;

    esp_err_t err = candis_power_release(&s_charge_setup);
    if (err == ESP_OK) {
        err = open_pmu(&s_charge_setup);
    }
    uint8_t chip_id = 0;
    if (err == ESP_OK) {
        err = tg28_esp_read_chip_id(&s_charge_setup.pmu, &chip_id);
    }
    if (err == ESP_OK && chip_id != TG28_ESP_CHIP_ID) {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    uint16_t current_ma = 0;
    if (err == ESP_OK) {
        unsigned valid_samples = 0;
        esp_err_t sample_err = ESP_ERR_INVALID_RESPONSE;
        for (unsigned attempt = 0; attempt < 5 && valid_samples < 3; ++attempt) {
            tg28_esp_status_t status;
            tg28_esp_charger_t charger;
            tg28_esp_irq_t irq;
            sample_err = tg28_esp_read_status(&s_charge_setup.pmu, &status);
            if (sample_err == ESP_OK) {
                sample_err = tg28_esp_read_charger(&s_charge_setup.pmu, &charger);
            }
            if (sample_err == ESP_OK) {
                sample_err = tg28_esp_read_irq(&s_charge_setup.pmu, &irq);
            }
            if (sample_err == ESP_OK && !charge_profile_ready(&status, &charger, &irq)) {
                sample_err = ESP_ERR_INVALID_STATE;
            }
            if (sample_err == ESP_OK && charger.current_ma != fallback_ma && charger.current_ma != target_ma) {
                sample_err = ESP_ERR_INVALID_STATE;
            }
            if (sample_err != ESP_OK) {
                valid_samples = 0;
                if (sample_err == ESP_ERR_INVALID_STATE) {
                    break;
                }
            } else {
                if (charger.current_ma != current_ma) {
                    valid_samples = 0;
                }
                current_ma = charger.current_ma;
                ++valid_samples;
            }
            if (valid_samples < 3 && attempt < 4) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        if (valid_samples < 3) {
            err = sample_err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : sample_err;
        }
    }

    if (err == ESP_OK && current_ma == fallback_ma) {
        err = tg28_esp_set_charge_current(&s_charge_setup.pmu, target_ma);
        uint16_t readback_ma = 0;
        esp_err_t readback = err;
        if (readback == ESP_OK) {
            readback = tg28_esp_read_charge_current(&s_charge_setup.pmu, &readback_ma);
        }
        if (readback != ESP_OK || readback_ma != target_ma) {
            esp_err_t restore = tg28_esp_set_charge_current(&s_charge_setup.pmu, fallback_ma);
            uint16_t restored_ma = 0;
            if (restore == ESP_OK) {
                restore = tg28_esp_read_charge_current(&s_charge_setup.pmu, &restored_ma);
                if (restore == ESP_OK && restored_ma != fallback_ma) {
                    restore = ESP_ERR_INVALID_RESPONSE;
                }
            }
            ESP_LOGE(TAG, "Set charge %u mA failed: %s, readback=%u mA, restore=%s, readback=%u mA",
                     target_ma, esp_err_to_name(readback), readback_ma, esp_err_to_name(restore), restored_ma);
            err = readback == ESP_OK ? ESP_ERR_INVALID_RESPONSE : readback;
        } else {
            ESP_LOGI(TAG, "Charge current %u -> %u mA, readback=%u mA", fallback_ma, target_ma, readback_ma);
        }
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Charge current already %u mA", current_ma);
    }

    const esp_err_t cleanup = candis_power_release(&s_charge_setup);
    if (err == ESP_OK && cleanup == ESP_OK) {
        s_default_charge_applied = true;
    }
    return err == ESP_OK ? cleanup : err;
}

void candis_power_init_default_charge_locked(void)
{
    if (s_default_charge_applied || s_default_charge_init_attempts >= 3) {
        return;
    }
    ++s_default_charge_init_attempts;
    const esp_err_t err = ensure_default_charge_locked();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Default charge setup deferred: %s (%u/3)", esp_err_to_name(err),
                 s_default_charge_init_attempts);
    }
}

esp_err_t candis_power_ensure_default_charge(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = ensure_default_charge_locked();
    candis_control_leave();
    return err;
}
