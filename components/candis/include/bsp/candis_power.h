/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool panel;
    bool audio;
    bool aldo3;
    bool dc1sw;
} candis_power_state_t;

/** Snapshot board rails. This call does not write the PMU. */
esp_err_t candis_power_read(candis_power_state_t *state);

typedef struct {
    bool vbus_good;
    bool system_on;
    bool battery_present;
    bool battery_charging;
    bool bldo2_on;
    uint16_t bldo2_mv;
} candis_supply_state_t;

/** Read TG28 supply and charge status, and the BLDO2 programmed setpoint. */
esp_err_t candis_power_read_supply(candis_supply_state_t *state);

/** Read VBAT in microvolts when a battery is present and the VBAT ADC is already enabled. */
esp_err_t candis_power_read_battery_uv(uint32_t *microvolts);

#define CANDIS_OTP_CHARGE_CURRENT_MA 50
#define CANDIS_DEFAULT_CHARGE_CURRENT_MA 200

/**
 * @brief Apply the board's 200 mA charge-current default after validating the charger.
 *
 * The TG28-ESP OTP value for this board is 50 mA. This call changes only
 * REG62[4:0] after three consecutive samples establish that VBUS and the battery are
 * present, the charger is enabled, CV is 4.20 V, safety timers are enabled,
 * and no charger or input-limit fault is active. It then reads the setting back.
 * Call from a control task when VBUS and the battery become available following startup.
 */
esp_err_t candis_power_ensure_default_charge(void);

/** Acquire the legacy singleton application reference to DC1SW.
 * Set manage_power only when the attached loads are known. If acquisition fails,
 * call release to retry any pending rollback before trying again.
 */
esp_err_t candis_expansion_power_acquire(bool manage_power);

/** Release the application reference after its peripherals are idle. Retry on failure. */
esp_err_t candis_expansion_power_release(void);

typedef struct candis_expansion_power_lease *candis_expansion_power_handle_t;

/**
 * @brief Acquire a private DC1SW reference for an application or component.
 * @param[in] manage_power Allow enabling DC1SW after checking all attached loads.
 * @param[out] handle Empty handle slot owned by this caller. Do not copy or reuse a live handle.
 * @return ESP_OK on success. On error, a non-NULL handle requires unclaim to retry rollback.
 */
esp_err_t candis_expansion_power_claim(bool manage_power, candis_expansion_power_handle_t *handle);

/**
 * @brief Release the caller's reference after its clients have stopped using DC1SW.
 * @return ESP_OK and a cleared handle on success, including an already cleared handle.
 *         On error the handle remains valid for a retry.
 */
esp_err_t candis_expansion_power_unclaim(candis_expansion_power_handle_t *handle);

typedef enum {
    CANDIS_EXPANSION_ALDO3,
    CANDIS_EXPANSION_ALDO4,
    CANDIS_EXPANSION_BLDO1,
    CANDIS_EXPANSION_BLDO2,
} candis_expansion_ldo_t;

/** Value token identifying one expansion LDO lease generation. */
typedef uintptr_t candis_expansion_ldo_handle_t;
#define CANDIS_EXPANSION_LDO_HANDLE_NONE ((candis_expansion_ldo_handle_t)0)

/**
 * Claim an expansion LDO exclusively. The attached expansion must support the
 * requested voltage (500–3500 mV, in 100 mV steps).
 *
 * An already enabled rail is accepted only at the requested setting and stays
 * enabled on release. Initialize the output slot to CANDIS_EXPANSION_LDO_HANDLE_NONE.
 * A token returned on error represents a partial lease. Pass it to
 * candis_expansion_ldo_release() to retry cleanup. An output slot containing
 * an active token is preserved and rejected with ESP_ERR_INVALID_STATE.
 * The caller must clear the released output slot with
 * CANDIS_EXPANSION_LDO_HANDLE_NONE before reusing it.
 */
esp_err_t candis_expansion_ldo_acquire(candis_expansion_ldo_t rail, uint16_t voltage_mv, unsigned settle_ms,
                                      candis_expansion_ldo_handle_t *handle);

/** Restore the original rail state. Retry with the same token if cleanup fails.
 * A release request for an expired token returns ESP_ERR_INVALID_STATE.
 */
esp_err_t candis_expansion_ldo_release(candis_expansion_ldo_handle_t handle);

#ifdef __cplusplus
}
#endif
