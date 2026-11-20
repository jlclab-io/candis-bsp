/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TG28_ESP_DEFAULT_ADDRESS 0x34
#define TG28_ESP_CHIP_ID 0x4a

typedef enum {
    TG28_ESP_ENABLE_ALDO1 = 0x01,
    TG28_ESP_ENABLE_ALDO2 = 0x02,
    TG28_ESP_ENABLE_ALDO3 = 0x04,
    TG28_ESP_ENABLE_ALDO4 = 0x08,
    TG28_ESP_ENABLE_BLDO1 = 0x10,
    TG28_ESP_ENABLE_BLDO2 = 0x20,
    TG28_ESP_ENABLE_CPUSLDO = 0x40,
    TG28_ESP_ENABLE_DC1SW = 0x80,
} tg28_esp_output_t;

typedef enum {
    TG28_ESP_ALDO1 = 0x92,
    TG28_ESP_ALDO2 = 0x93,
    TG28_ESP_ALDO3 = 0x94,
    TG28_ESP_ALDO4 = 0x95,
    TG28_ESP_BLDO1 = 0x96,
    TG28_ESP_BLDO2 = 0x97,
} tg28_esp_ldo_t;

enum {
    TG28_ESP_LDO_MIN_MV = 500,
    TG28_ESP_LDO_MAX_MV = 3500,
    TG28_ESP_LDO_STEP_MV = 100,
};

typedef struct {
    uint8_t address;
    uint32_t scl_speed_hz;
    int timeout_ms;
    bool allow_protection_changes; /**< Opt in to protected setters and power commands. */
} tg28_esp_config_t;

/** Default I2C transport. The host leaves all PMU power-on settings in place. */
#define TG28_ESP_CONFIG_DEFAULT() { \
    .address = TG28_ESP_DEFAULT_ADDRESS, .scl_speed_hz = 100000, .timeout_ms = 100, \
}

typedef struct {
    i2c_master_dev_handle_t device;
    int timeout_ms;
    bool allow_protection_changes;
} tg28_esp_t;

/**
 * Attach a zero-initialized context to a caller-owned bus without accessing
 * registers. Keep the bus alive until close succeeds. Serialize calls to the
 * same PMU across all contexts, including any direct register access.
 */
esp_err_t tg28_esp_open(tg28_esp_t *pmu, i2c_master_bus_handle_t bus,
                       const tg28_esp_config_t *config);

/** Detach the device. On failure the caller may retry with the same context. */
esp_err_t tg28_esp_close(tg28_esp_t *pmu);

/** Read REG90. Each enabled output is represented by a tg28_esp_output_t bit. */
esp_err_t tg28_esp_read_outputs(const tg28_esp_t *pmu, uint8_t *enabled);

/** Update only the selected REG90 bits and skip the write when already equal. */
esp_err_t tg28_esp_set_outputs(const tg28_esp_t *pmu, uint8_t mask, bool enabled);

/** Read the five-bit voltage field, including any reserved code read back. */
esp_err_t tg28_esp_read_ldo_code(const tg28_esp_t *pmu, tg28_esp_ldo_t ldo, uint8_t *code);

/**
 * Set the five-bit voltage field. Use codes 0 through 30 for new voltage
 * settings. Code 31 is reserved and is accepted solely for exact rollback.
 */
esp_err_t tg28_esp_set_ldo_code(const tg28_esp_t *pmu, tg28_esp_ldo_t ldo, uint8_t code);

typedef enum {
    TG28_ESP_TRICKLE,
    TG28_ESP_PRECHARGE,
    TG28_ESP_CONSTANT_CURRENT,
    TG28_ESP_CONSTANT_VOLTAGE,
    TG28_ESP_CHARGE_DONE,
    TG28_ESP_NOT_CHARGING,
    TG28_ESP_CHARGE_STATE_UNKNOWN,
} tg28_esp_charge_state_t;

typedef enum {
    TG28_ESP_BATTERY_STANDBY,
    TG28_ESP_BATTERY_CHARGING,
    TG28_ESP_BATTERY_DISCHARGING,
    TG28_ESP_BATTERY_DIRECTION_UNKNOWN,
} tg28_esp_battery_direction_t;

typedef struct {
    bool vbus_good;
    bool battery_present;
    bool batfet_open;
    bool battery_active;
    bool thermal_regulation;
    bool input_current_limit;
    bool input_voltage_limit;
    bool system_on;
    tg28_esp_battery_direction_t direction;
    tg28_esp_charge_state_t charge_state;
} tg28_esp_status_t;

esp_err_t tg28_esp_read_chip_id(const tg28_esp_t *pmu, uint8_t *id);
/** Read REG03 and compare with the TG28-ESP ID observed on Candis. No writes. */
esp_err_t tg28_esp_check_chip_id(const tg28_esp_t *pmu);
esp_err_t tg28_esp_read_status(const tg28_esp_t *pmu, tg28_esp_status_t *status);
esp_err_t tg28_esp_read_power_sources(const tg28_esp_t *pmu, uint8_t *power_on, uint8_t *power_off);
/** Return the enabled DCDC1–4 bitmap in bits 0–3. */
esp_err_t tg28_esp_read_dcdc_outputs(const tg28_esp_t *pmu, uint8_t *enabled);

typedef enum {
    TG28_ESP_RAIL_DCDC1,
    TG28_ESP_RAIL_DCDC2,
    TG28_ESP_RAIL_DCDC3,
    TG28_ESP_RAIL_DCDC4,
    TG28_ESP_RAIL_ALDO1,
    TG28_ESP_RAIL_ALDO2,
    TG28_ESP_RAIL_ALDO3,
    TG28_ESP_RAIL_ALDO4,
    TG28_ESP_RAIL_BLDO1,
    TG28_ESP_RAIL_BLDO2,
    TG28_ESP_RAIL_CPUSLDO,
    TG28_ESP_RAIL_DC1SW,
    TG28_ESP_RAIL_DC4SW,
} tg28_esp_rail_t;

esp_err_t tg28_esp_read_rail_enabled(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *enabled);
/** The caller owns supply sequencing and must identify rails feeding the MCU and PMU bus. */
esp_err_t tg28_esp_set_rail_enabled(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool enabled);
/** Read the programmed setpoint, including the parent DCDC setpoint for a switch. */
esp_err_t tg28_esp_read_rail_voltage(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint16_t *mv);
/** Exact mV setpoint. DC1SW and DC4SW follow their upstream supplies. */
esp_err_t tg28_esp_set_rail_voltage(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint16_t mv);
esp_err_t tg28_esp_read_dcdc_pwm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *forced);
esp_err_t tg28_esp_set_dcdc_pwm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool forced);

typedef enum {
    TG28_ESP_ADC_VBAT = 1 << 0,
    TG28_ESP_ADC_TS = 1 << 1,
    TG28_ESP_ADC_VBUS = 1 << 2,
    TG28_ESP_ADC_VSYS = 1 << 3,
    TG28_ESP_ADC_DIE = 1 << 4,
    TG28_ESP_ADC_GP = 1 << 5,
} tg28_esp_adc_channel_t;

esp_err_t tg28_esp_read_adc_mask(const tg28_esp_t *pmu, uint8_t *enabled);
esp_err_t tg28_esp_set_adc_channels(const tg28_esp_t *pmu, uint8_t mask, bool enabled);
/** Read one enabled channel. GPADC has no documented sample registers in V1.0. */
esp_err_t tg28_esp_read_adc_raw(const tg28_esp_t *pmu, tg28_esp_adc_channel_t channel, uint16_t *raw);
/** Return ADC input voltage. TS and DIE readings are sensor voltages.
 * TS overflow returns ESP_ERR_INVALID_RESPONSE. Use read_adc_raw for diagnostics. */
esp_err_t tg28_esp_read_adc_uv(const tg28_esp_t *pmu, tg28_esp_adc_channel_t channel, uint32_t *uv);
/** Estimate PMU die temperature in m°C from the vendor FAQ conversion. */
esp_err_t tg28_esp_read_die_temperature(const tg28_esp_t *pmu, int32_t *millicelsius);

typedef struct {
    uint8_t bank[3];
} tg28_esp_irq_t;

esp_err_t tg28_esp_read_irq(const tg28_esp_t *pmu, tg28_esp_irq_t *irq);
esp_err_t tg28_esp_read_irq_enable(const tg28_esp_t *pmu, tg28_esp_irq_t *irq);
esp_err_t tg28_esp_set_irq_enable(const tg28_esp_t *pmu, const tg28_esp_irq_t *irq);
/** Acknowledge only the specified W1C flags. Calling with pending flags may release IRQ. */
esp_err_t tg28_esp_clear_irq(const tg28_esp_t *pmu, const tg28_esp_irq_t *irq);

typedef struct {
    uint16_t input_limit_ma;
    uint16_t input_limit_mv;
    uint16_t precharge_ma;
    uint16_t current_ma;
    uint16_t termination_ma;
    uint16_t voltage_mv;
    bool termination_enabled;
    bool enabled;
    bool detection_enabled;
    bool ts_protection_selected;
    bool jeita_enabled;
    bool precharge_timer_enabled;
    bool charge_timer_enabled;
} tg28_esp_charger_t;

/** Read charger settings in mA and mV. Current fields are register setpoints. */
esp_err_t tg28_esp_read_charger(const tg28_esp_t *pmu, tg28_esp_charger_t *charger);
esp_err_t tg28_esp_read_charge_current(const tg28_esp_t *pmu, uint16_t *ma);
/** Write only the CC setpoint. Accept 0–200 mA in 25 mA steps, 300–1500 in 100 mA steps.
 * The caller must enforce battery, supply and CV-dependent current limits. */
esp_err_t tg28_esp_set_charge_current(const tg28_esp_t *pmu, uint16_t ma);
esp_err_t tg28_esp_set_charger_enabled(const tg28_esp_t *pmu, bool enabled);
esp_err_t tg28_esp_set_input_current_limit(const tg28_esp_t *pmu, uint16_t ma);
esp_err_t tg28_esp_set_input_voltage_limit(const tg28_esp_t *pmu, uint16_t mv);
esp_err_t tg28_esp_set_precharge_current(const tg28_esp_t *pmu, uint16_t ma);
esp_err_t tg28_esp_set_charge_termination(const tg28_esp_t *pmu, uint16_t ma, bool enabled);
esp_err_t tg28_esp_set_charge_voltage(const tg28_esp_t *pmu, uint16_t mv);
esp_err_t tg28_esp_set_battery_detection(const tg28_esp_t *pmu, bool enabled);
esp_err_t tg28_esp_read_soc(const tg28_esp_t *pmu, uint8_t *percent);
esp_err_t tg28_esp_set_fuel_gauge(const tg28_esp_t *pmu, bool enabled);

typedef struct {
    bool enabled;
    bool sram_selected;
    bool parameter_access_enabled;
} tg28_esp_gauge_config_t;

/** Read REG18[3] and REG A2. SRAM selection alone does not validate its contents. */
esp_err_t tg28_esp_read_gauge_config(const tg28_esp_t *pmu, tg28_esp_gauge_config_t *config);

typedef enum {
    TG28_ESP_CHGLED_TYPE_A,
    TG28_ESP_CHGLED_TYPE_B,
    TG28_ESP_CHGLED_MANUAL,
} tg28_esp_chgled_mode_t;

typedef enum {
    TG28_ESP_CHGLED_HIGH_Z,
    TG28_ESP_CHGLED_BLINK_1HZ,
    TG28_ESP_CHGLED_BLINK_4HZ,
    TG28_ESP_CHGLED_LOW,
} tg28_esp_chgled_output_t;

typedef struct {
    bool enabled;
    tg28_esp_chgled_mode_t mode;
    tg28_esp_chgled_output_t output;
} tg28_esp_chgled_t;

esp_err_t tg28_esp_read_chgled(const tg28_esp_t *pmu, tg28_esp_chgled_t *led);
esp_err_t tg28_esp_set_chgled(const tg28_esp_t *pmu, const tg28_esp_chgled_t *led);

typedef struct {
    uint16_t on_ms;    /**< 128, 512, 1000 or 2000 ms. */
    uint16_t irq_ms;   /**< 1000, 1500, 2000 or 2500 ms. */
    uint16_t off_ms;   /**< 4000, 6000, 8000 or 10000 ms. */
} tg28_esp_power_key_t;

esp_err_t tg28_esp_read_power_key(const tg28_esp_t *pmu, tg28_esp_power_key_t *timing);
esp_err_t tg28_esp_set_power_key(const tg28_esp_t *pmu, const tg28_esp_power_key_t *timing);

typedef struct {
    bool irq_wakeup;
    bool pull_pwrok_low;
    bool restore_previous_voltage;
} tg28_esp_sleep_t;

esp_err_t tg28_esp_read_sleep_config(const tg28_esp_t *pmu, tg28_esp_sleep_t *config);
/** Read the two REG26 control bits. They do not report SoC sleep or rail voltage. */
esp_err_t tg28_esp_read_sleep_controls(const tg28_esp_t *pmu, bool *sleep, bool *wake);
/** Latch the PMU sleep state. The caller manages rail changes and SoC sleep. */
esp_err_t tg28_esp_enter_sleep(const tg28_esp_t *pmu, const tg28_esp_sleep_t *config);
/** Request wake from an armed PMU while keeping SLEEP_EN asserted in the write. */
esp_err_t tg28_esp_request_wakeup(const tg28_esp_t *pmu, const tg28_esp_sleep_t *config);

typedef enum {
    TG28_ESP_WATCHDOG_IRQ,
    TG28_ESP_WATCHDOG_SYSTEM_RESET,
    TG28_ESP_WATCHDOG_SYSTEM_RESET_PWROK,
    TG28_ESP_WATCHDOG_POWER_CYCLE_RAILS,
} tg28_esp_watchdog_action_t;

esp_err_t tg28_esp_read_watchdog(const tg28_esp_t *pmu, bool *enabled, uint16_t *seconds,
                                tg28_esp_watchdog_action_t *action);
/** Configuration does not enable the watchdog. Reset actions require opt-in. */
esp_err_t tg28_esp_configure_watchdog(const tg28_esp_t *pmu, uint16_t seconds,
                                     tg28_esp_watchdog_action_t action);
esp_err_t tg28_esp_set_watchdog_enabled(const tg28_esp_t *pmu, bool enabled);
esp_err_t tg28_esp_feed_watchdog(const tg28_esp_t *pmu);

/** Sequential read. Follow the access rules for the selected registers. */
esp_err_t tg28_esp_read_registers(const tg28_esp_t *pmu, uint8_t reg, uint8_t *data, size_t size);

/** Four bytes of scratch storage. PMU power-on reset clears the buffer. */
esp_err_t tg28_esp_read_buffer(const tg28_esp_t *pmu, uint8_t data[4]);
esp_err_t tg28_esp_write_buffer(const tg28_esp_t *pmu, const uint8_t data[4]);

/** An open-drain output. false selects high impedance. Check the package pinout. */
esp_err_t tg28_esp_read_gpio1(const tg28_esp_t *pmu, bool *drive_low);
esp_err_t tg28_esp_set_gpio1(const tg28_esp_t *pmu, bool drive_low);

/** Warning levels are 0–15% for level 1 and 5–20% for level 2. */
esp_err_t tg28_esp_read_soc_thresholds(const tg28_esp_t *pmu, uint8_t *level1, uint8_t *level2);
esp_err_t tg28_esp_set_soc_thresholds(const tg28_esp_t *pmu, uint8_t level1, uint8_t level2);
esp_err_t tg28_esp_reset_fuel_gauge(const tg28_esp_t *pmu);

typedef enum {
    TG28_ESP_IRQ_BAT_COLD_WORK = 1U << 0,
    TG28_ESP_IRQ_BAT_HOT_WORK = 1U << 1,
    TG28_ESP_IRQ_BAT_COLD_CHARGE = 1U << 2,
    TG28_ESP_IRQ_BAT_HOT_CHARGE = 1U << 3,
    TG28_ESP_IRQ_NEW_SOC = 1U << 4,
    TG28_ESP_IRQ_GAUGE_WATCHDOG = 1U << 5,
    TG28_ESP_IRQ_SOC_LEVEL1 = 1U << 6,
    TG28_ESP_IRQ_SOC_LEVEL2 = 1U << 7,
    TG28_ESP_IRQ_PWRON_RISING = 1U << 8,
    TG28_ESP_IRQ_PWRON_FALLING = 1U << 9,
    TG28_ESP_IRQ_PWRON_LONG = 1U << 10,
    TG28_ESP_IRQ_PWRON_SHORT = 1U << 11,
    TG28_ESP_IRQ_BAT_REMOVE = 1U << 12,
    TG28_ESP_IRQ_BAT_INSERT = 1U << 13,
    TG28_ESP_IRQ_VBUS_REMOVE = 1U << 14,
    TG28_ESP_IRQ_VBUS_INSERT = 1U << 15,
    TG28_ESP_IRQ_BAT_OVP = 1U << 16,
    TG28_ESP_IRQ_CHARGER_TIMEOUT = 1U << 17,
    TG28_ESP_IRQ_DIE_HOT = 1U << 18,
    TG28_ESP_IRQ_CHARGE_START = 1U << 19,
    TG28_ESP_IRQ_CHARGE_DONE = 1U << 20,
    TG28_ESP_IRQ_LDO_OVERCURRENT = 1U << 22,
    TG28_ESP_IRQ_WATCHDOG = 1U << 23,
} tg28_esp_irq_event_t;

#define TG28_ESP_IRQ_ALL 0xdfffffU

typedef void (*tg28_esp_irq_callback_t)(uint32_t events, void *user);

/** Deliver matching latched flags in the calling task. The caller owns GPIO/ISR and W1C. */
esp_err_t tg28_esp_dispatch_irq(const tg28_esp_t *pmu, uint32_t filter,
                               tg28_esp_irq_callback_t callback, void *user);
/** Update only selected enable bits (REG40–42). An I2C error can leave earlier banks updated. */
esp_err_t tg28_esp_update_irq_events(const tg28_esp_t *pmu, uint32_t events, bool enabled);
esp_err_t tg28_esp_ack_events(const tg28_esp_t *pmu, uint32_t events);

typedef enum {
    TG28_ESP_POWER_OFF,
    TG28_ESP_POWER_RESTART,
} tg28_esp_power_command_t;

/** Requires opt-in. The host may lose power before the I2C transaction completes. */
esp_err_t tg28_esp_request_power(const tg28_esp_t *pmu, tg28_esp_power_command_t command);

typedef struct {
    bool discharge_outputs;
    bool pwrok_reset;
    bool pwron_16s_shutdown;
} tg28_esp_power_control_t;

esp_err_t tg28_esp_read_power_control(const tg28_esp_t *pmu, tg28_esp_power_control_t *control);
esp_err_t tg28_esp_set_power_control(const tg28_esp_t *pmu, const tg28_esp_power_control_t *control);

typedef struct {
    bool die_shutdown;
    bool key_shutdown;
    bool key_restarts;
} tg28_esp_shutdown_control_t;

esp_err_t tg28_esp_read_shutdown_control(const tg28_esp_t *pmu, tg28_esp_shutdown_control_t *control);
esp_err_t tg28_esp_set_shutdown_control(const tg28_esp_t *pmu, const tg28_esp_shutdown_control_t *control);

/** DCDC UVP bits 0–3 map to DCDC1–4. Protection setters require opt-in. */
esp_err_t tg28_esp_read_dcdc_protection(const tg28_esp_t *pmu, bool *ovp, uint8_t *uvp);
esp_err_t tg28_esp_set_dcdc_protection(const tg28_esp_t *pmu, bool ovp, uint8_t uvp);
esp_err_t tg28_esp_read_shutdown_voltage(const tg28_esp_t *pmu, uint16_t *mv);
esp_err_t tg28_esp_set_shutdown_voltage(const tg28_esp_t *pmu, uint16_t mv);

typedef struct {
    uint8_t delay_ms; /**< 8, 16, 32 or 64 ms after outputs are valid. */
    bool check_after_128ms;
    bool shutdown_delay_4ms;
    bool reverse_shutdown_order;
} tg28_esp_pwrok_t;

esp_err_t tg28_esp_read_pwrok(const tg28_esp_t *pmu, tg28_esp_pwrok_t *config);
esp_err_t tg28_esp_set_pwrok(const tg28_esp_t *pmu, const tg28_esp_pwrok_t *config);

/** Codes 0–2 select hardware slots, 3 disables a rail in the fast sequence. */
esp_err_t tg28_esp_read_fast_sequence(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint8_t *slot);
esp_err_t tg28_esp_set_fast_sequence(const tg28_esp_t *pmu, tg28_esp_rail_t rail, uint8_t slot);
esp_err_t tg28_esp_read_fast_start(const tg28_esp_t *pmu, bool *power_on, bool *wake);
esp_err_t tg28_esp_set_fast_start(const tg28_esp_t *pmu, bool power_on, bool wake);

/** DVM is supported on DCDC2 and DCDC3. The ramp setting is shared. */
esp_err_t tg28_esp_read_dvm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool *enabled);
esp_err_t tg28_esp_set_dvm(const tg28_esp_t *pmu, tg28_esp_rail_t rail, bool enabled);
esp_err_t tg28_esp_read_dcdc_control(const tg28_esp_t *pmu, bool *ccm, bool *slow_ramp);
esp_err_t tg28_esp_set_dcdc_control(const tg28_esp_t *pmu, bool ccm, bool slow_ramp);
esp_err_t tg28_esp_read_dcdc_spread(const tg28_esp_t *pmu, bool *enabled, uint8_t *khz);
esp_err_t tg28_esp_set_dcdc_spread(const tg28_esp_t *pmu, bool enabled, uint8_t khz);
esp_err_t tg28_esp_read_dcdc_uvp_delay(const tg28_esp_t *pmu, uint16_t *us);
esp_err_t tg28_esp_set_dcdc_uvp_delay(const tg28_esp_t *pmu, uint16_t us);

/** Power path setters require opt-in. */
esp_err_t tg28_esp_read_batfet_power_off(const tg28_esp_t *pmu, bool *enabled);
esp_err_t tg28_esp_set_batfet_power_off(const tg28_esp_t *pmu, bool enabled);
esp_err_t tg28_esp_read_min_system_voltage(const tg28_esp_t *pmu, uint16_t *mv);
esp_err_t tg28_esp_set_min_system_voltage(const tg28_esp_t *pmu, uint16_t mv);
esp_err_t tg28_esp_read_backup_charge(const tg28_esp_t *pmu, bool *enabled, uint16_t *mv);
esp_err_t tg28_esp_set_backup_charge_enabled(const tg28_esp_t *pmu, bool enabled);
esp_err_t tg28_esp_set_backup_charge_voltage(const tg28_esp_t *pmu, uint16_t mv);

typedef struct {
    uint8_t precharge_min; /**< 40, 50, 60 or 70 minutes. */
    uint8_t charge_hr; /**< 5, 8, 12 or 20 hours. */
    bool precharge_enabled;
    bool charge_enabled;
    bool slow_under_limiting;
} tg28_esp_safety_timer_t;

/** Thermal and timer setters require opt-in. */
esp_err_t tg28_esp_read_safety_timer(const tg28_esp_t *pmu, tg28_esp_safety_timer_t *timer);
esp_err_t tg28_esp_set_safety_timer(const tg28_esp_t *pmu, const tg28_esp_safety_timer_t *timer);
esp_err_t tg28_esp_read_die_protection(const tg28_esp_t *pmu, bool *enabled, uint8_t *celsius);
esp_err_t tg28_esp_set_die_protection(const tg28_esp_t *pmu, bool enabled, uint8_t celsius);
esp_err_t tg28_esp_read_charge_thermal_limit(const tg28_esp_t *pmu, uint8_t *celsius);
esp_err_t tg28_esp_set_charge_thermal_limit(const tg28_esp_t *pmu, uint8_t celsius);

typedef enum {
    TG28_ESP_TS_BIAS_OFF,
    TG28_ESP_TS_BIAS_ADC_ENABLED,
    TG28_ESP_TS_BIAS_ADC_SAMPLING,
    TG28_ESP_TS_BIAS_ALWAYS,
} tg28_esp_ts_bias_mode_t;

typedef struct {
    bool external_input;
    tg28_esp_ts_bias_mode_t bias_mode;
    uint8_t bias_ua;
} tg28_esp_ts_t;

esp_err_t tg28_esp_read_ts(const tg28_esp_t *pmu, tg28_esp_ts_t *ts);
esp_err_t tg28_esp_set_ts(const tg28_esp_t *pmu, const tg28_esp_ts_t *ts);
/** Read the programmed TS ADC substitute in microvolts (REG5C–5D). */
esp_err_t tg28_esp_read_ts_override_uv(const tg28_esp_t *pmu, uint32_t *uv);

typedef struct {
    uint32_t uv;
    int32_t millicelsius;
} tg28_esp_ts_point_t;

/**
 * Read the enabled TS ADC and interpolate a board-specific calibration curve.
 * Supply at least two points with strictly increasing voltage, calibrated for
 * the fitted sensor, bias and resistor network. Out-of-curve samples return
 * ESP_ERR_INVALID_RESPONSE. Allow the ADC to settle after changes.
 */
esp_err_t tg28_esp_read_ts_temperature(const tg28_esp_t *pmu, const tg28_esp_ts_point_t *curve,
                                       size_t count, int32_t *millicelsius);

typedef enum {
    TG28_ESP_TS_COLD_HYSTERESIS,
    TG28_ESP_TS_HOT_HYSTERESIS,
    TG28_ESP_TS_CHARGE_COLD,
    TG28_ESP_TS_CHARGE_HOT,
    TG28_ESP_TS_WORK_COLD,
    TG28_ESP_TS_WORK_HOT,
    TG28_ESP_TS_JEITA_COOL,
    TG28_ESP_TS_JEITA_WARM,
} tg28_esp_ts_threshold_t;

/** Exact TS input voltage in mV. TS and JEITA setters require opt-in. */
esp_err_t tg28_esp_read_ts_threshold(const tg28_esp_t *pmu, tg28_esp_ts_threshold_t threshold, uint16_t *mv);
esp_err_t tg28_esp_set_ts_threshold(const tg28_esp_t *pmu, tg28_esp_ts_threshold_t threshold, uint16_t mv);
esp_err_t tg28_esp_read_jeita_enabled(const tg28_esp_t *pmu, bool *enabled);
esp_err_t tg28_esp_set_jeita_enabled(const tg28_esp_t *pmu, bool enabled);

typedef struct {
    bool cool_half_current;
    bool warm_half_current;
    uint8_t cool_voltage_steps;
    uint8_t warm_voltage_steps;
} tg28_esp_jeita_t;

esp_err_t tg28_esp_read_jeita(const tg28_esp_t *pmu, tg28_esp_jeita_t *derating);
esp_err_t tg28_esp_set_jeita(const tg28_esp_t *pmu, const tg28_esp_jeita_t *derating);

#ifdef __cplusplus
}
#endif
