/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "api_suites.h"
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "tg28_esp.h"

#define TAPI "tg28-esp"
#define T_REQ(api, aspect, call, expected, detail) \
    api_test_require(TAPI, (api), (aspect), (call), (expected), (detail))
#define T_BOOL(api, aspect, condition, detail) \
    api_test_check_bool(TAPI, (api), (aspect), (condition), (detail))
#define T_BLOCK(api, aspect, detail) API_BLOCKED(TAPI, (api), (aspect), (detail))
#define T_NA(api, aspect, detail) API_NA(TAPI, (api), (aspect), (detail))

static tg28_esp_t s_pmu;
static uint32_t s_irq_events;
static unsigned s_irq_callbacks;

static void irq_callback(uint32_t events, void *user)
{
    uint32_t *captured = user;
    *captured = events;
    ++s_irq_callbacks;
}

static bool tg28_open_common(bool allow_protection)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    config.timeout_ms = 200;
    config.allow_protection_changes = allow_protection;
    return bus && tg28_esp_open(&s_pmu, bus, &config) == ESP_OK;
}

static void test_identity(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    tg28_esp_config_t config = TG28_ESP_CONFIG_DEFAULT();
    tg28_esp_t pmu = {0};
    T_REQ("tg28_esp_open", API_ASPECT_BOUNDARY, tg28_esp_open(NULL, bus, &config),
          ESP_ERR_INVALID_ARG, "NULL context");
    T_REQ("tg28_esp_open", API_ASPECT_BOUNDARY, tg28_esp_open(&pmu, NULL, &config),
          ESP_ERR_INVALID_ARG, "NULL bus");
    T_REQ("tg28_esp_open", API_ASPECT_BOUNDARY, tg28_esp_open(&pmu, bus, NULL),
          ESP_ERR_INVALID_ARG, "NULL config");
    config.address = 0x77;
    config.scl_speed_hz = 0;
    T_REQ("tg28_esp_open", API_ASPECT_BOUNDARY, tg28_esp_open(&pmu, bus, &config),
          ESP_ERR_INVALID_ARG, "zero I2C speed");
    config = (tg28_esp_config_t)TG28_ESP_CONFIG_DEFAULT();
    config.timeout_ms = 200;
    T_REQ("tg28_esp_open", API_ASPECT_NORMAL, tg28_esp_open(&pmu, bus, &config),
          ESP_OK, "attach address 0x34");
    T_REQ("tg28_esp_open", API_ASPECT_LIFECYCLE, tg28_esp_open(&pmu, bus, &config),
          ESP_ERR_INVALID_STATE, "double open rejected");
    uint8_t id = 0;
    T_REQ("tg28_esp_read_chip_id", API_ASPECT_BOUNDARY,
          tg28_esp_read_chip_id(&pmu, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    T_REQ("tg28_esp_read_chip_id", API_ASPECT_NORMAL,
          tg28_esp_read_chip_id(&pmu, &id), ESP_OK, "read REG03");
    T_BOOL("tg28_esp_read_chip_id", API_ASPECT_HARDWARE, id == TG28_ESP_CHIP_ID,
           "physical TG28-ESP chip ID");
    T_REQ("tg28_esp_check_chip_id", API_ASPECT_NORMAL,
          tg28_esp_check_chip_id(&pmu), ESP_OK, "identity check");
    T_REQ("tg28_esp_check_chip_id", API_ASPECT_BOUNDARY,
          tg28_esp_check_chip_id(NULL), ESP_ERR_INVALID_ARG, "NULL context");
    T_REQ("tg28_esp_close", API_ASPECT_NORMAL, tg28_esp_close(&pmu), ESP_OK,
          "detach I2C device");
    T_REQ("tg28_esp_close", API_ASPECT_LIFECYCLE, tg28_esp_close(&pmu), ESP_OK,
          "repeated close");
    T_REQ("tg28_esp_close", API_ASPECT_BOUNDARY, tg28_esp_close(NULL),
          ESP_ERR_INVALID_ARG, "NULL context");
}

static void test_basic_reads_writes(void)
{
    uint8_t outputs = 0;
    T_REQ("tg28_esp_read_outputs", API_ASPECT_BOUNDARY,
          tg28_esp_read_outputs(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    T_REQ("tg28_esp_read_outputs", API_ASPECT_NORMAL,
          tg28_esp_read_outputs(&s_pmu, &outputs), ESP_OK, "read REG90");
    T_REQ("tg28_esp_set_outputs", API_ASPECT_NORMAL,
          tg28_esp_set_outputs(&s_pmu, 0, false), ESP_OK, "zero mask is a no-op");

    uint8_t code = 0;
    const tg28_esp_ldo_t ldo = TG28_ESP_ALDO3;
    T_REQ("tg28_esp_read_ldo_code", API_ASPECT_BOUNDARY,
          tg28_esp_read_ldo_code(&s_pmu, ldo, NULL), ESP_ERR_INVALID_ARG, "NULL code");
    T_REQ("tg28_esp_read_ldo_code", API_ASPECT_BOUNDARY,
          tg28_esp_read_ldo_code(&s_pmu, (tg28_esp_ldo_t)0x91, &code),
          ESP_ERR_INVALID_ARG, "invalid LDO enum");
    T_REQ("tg28_esp_read_ldo_code", API_ASPECT_NORMAL,
          tg28_esp_read_ldo_code(&s_pmu, ldo, &code), ESP_OK, "read ALDO3 code");
    T_REQ("tg28_esp_set_ldo_code", API_ASPECT_BOUNDARY,
          tg28_esp_set_ldo_code(&s_pmu, ldo, 32), ESP_ERR_INVALID_ARG, "code above 5-bit range");
    if (code <= 30) {
        T_REQ("tg28_esp_set_ldo_code", API_ASPECT_NORMAL,
              tg28_esp_set_ldo_code(&s_pmu, ldo, code), ESP_OK, "rewrite identical ALDO3 code");
    } else {
        T_BLOCK("tg28_esp_set_ldo_code", API_ASPECT_HARDWARE,
                "ALDO3 currently uses reserved code 31");
    }

    tg28_esp_status_t status = {0};
    T_REQ("tg28_esp_read_status", API_ASPECT_BOUNDARY,
          tg28_esp_read_status(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL status");
    T_REQ("tg28_esp_read_status", API_ASPECT_NORMAL,
          tg28_esp_read_status(&s_pmu, &status), ESP_OK, "read REG00/REG01");
    T_BOOL("tg28_esp_read_status", API_ASPECT_NORMAL,
           status.charge_state <= TG28_ESP_CHARGE_STATE_UNKNOWN &&
           status.direction <= TG28_ESP_BATTERY_DIRECTION_UNKNOWN,
           "enum fields decoded in range");

    uint8_t power_on = 0, power_off = 0;
    T_REQ("tg28_esp_read_power_sources", API_ASPECT_BOUNDARY,
          tg28_esp_read_power_sources(&s_pmu, NULL, &power_off),
          ESP_ERR_INVALID_ARG, "NULL power_on");
    T_REQ("tg28_esp_read_power_sources", API_ASPECT_NORMAL,
          tg28_esp_read_power_sources(&s_pmu, &power_on, &power_off), ESP_OK,
          "read REG20/REG21");
    uint8_t dcdc = 0;
    T_REQ("tg28_esp_read_dcdc_outputs", API_ASPECT_BOUNDARY,
          tg28_esp_read_dcdc_outputs(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    T_REQ("tg28_esp_read_dcdc_outputs", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_outputs(&s_pmu, &dcdc), ESP_OK, "read DCDC enable bitmap");
    T_BOOL("tg28_esp_read_dcdc_outputs", API_ASPECT_NORMAL, !(dcdc & 0xf0),
           "only bits 0-3 returned");

    uint8_t config[256];
    T_REQ("tg28_esp_read_registers", API_ASPECT_BOUNDARY,
          tg28_esp_read_registers(&s_pmu, 0, NULL, 1), ESP_ERR_INVALID_ARG, "NULL data");
    T_REQ("tg28_esp_read_registers", API_ASPECT_BOUNDARY,
          tg28_esp_read_registers(&s_pmu, 0xff, config, 2), ESP_ERR_INVALID_ARG,
          "range crosses register space");
    T_REQ("tg28_esp_read_registers", API_ASPECT_NORMAL,
          tg28_esp_read_registers(&s_pmu, 0, config, 0x80), ESP_OK,
          "sequential read of 128 registers");

    uint8_t scratch[4], original[4], pattern[4];
    T_REQ("tg28_esp_read_buffer", API_ASPECT_BOUNDARY,
          tg28_esp_read_buffer(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL buffer");
    T_REQ("tg28_esp_read_buffer", API_ASPECT_NORMAL,
          tg28_esp_read_buffer(&s_pmu, original), ESP_OK, "read scratch registers");
    for (size_t i = 0; i < 4; ++i) {
        pattern[i] = original[i] ^ 0xa5;
    }
    T_REQ("tg28_esp_write_buffer", API_ASPECT_BOUNDARY,
          tg28_esp_write_buffer(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL buffer");
    T_REQ("tg28_esp_write_buffer", API_ASPECT_NORMAL,
          tg28_esp_write_buffer(&s_pmu, pattern), ESP_OK, "write scratch pattern");
    T_REQ("tg28_esp_read_buffer", API_ASPECT_NORMAL,
          tg28_esp_read_buffer(&s_pmu, scratch), ESP_OK, "read back scratch pattern");
    T_BOOL("tg28_esp_write_buffer", API_ASPECT_HARDWARE,
           memcmp(scratch, pattern, sizeof(pattern)) == 0, "scratch readback matches");
    T_REQ("tg28_esp_write_buffer", API_ASPECT_LIFECYCLE,
          tg28_esp_write_buffer(&s_pmu, original), ESP_OK, "restore scratch registers");
    T_REQ("tg28_esp_read_buffer", API_ASPECT_LIFECYCLE,
          tg28_esp_read_buffer(&s_pmu, scratch), ESP_OK, "verify scratch restore");

    bool gpio1 = false;
    T_REQ("tg28_esp_read_gpio1", API_ASPECT_BOUNDARY,
          tg28_esp_read_gpio1(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    esp_err_t gpio_err = tg28_esp_read_gpio1(&s_pmu, &gpio1);
    if (gpio_err == ESP_OK) {
        api_test_log(TAPI, "tg28_esp_read_gpio1", API_ASPECT_NORMAL, API_PASS,
                     "GPIO1 mode decoded");
        T_REQ("tg28_esp_set_gpio1", API_ASPECT_NORMAL,
              tg28_esp_set_gpio1(&s_pmu, gpio1), ESP_OK, "rewrite identical GPIO1 mode");
    } else {
        T_REQ("tg28_esp_read_gpio1", API_ASPECT_ERROR, gpio_err,
              ESP_ERR_INVALID_RESPONSE, "reserved GPIO1 mode");
        T_BLOCK("tg28_esp_set_gpio1", API_ASPECT_HARDWARE,
                "GPIO1 mode reserved; no safe write");
    }
}

static void test_irq(void)
{
    tg28_esp_irq_t irq = {0};
    tg28_esp_irq_t enable = {0};
    T_REQ("tg28_esp_read_irq", API_ASPECT_BOUNDARY,
          tg28_esp_read_irq(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL struct");
    T_REQ("tg28_esp_read_irq", API_ASPECT_NORMAL,
          tg28_esp_read_irq(&s_pmu, &irq), ESP_OK, "read REG48-4A");
    T_REQ("tg28_esp_read_irq_enable", API_ASPECT_BOUNDARY,
          tg28_esp_read_irq_enable(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL struct");
    T_REQ("tg28_esp_read_irq_enable", API_ASPECT_NORMAL,
          tg28_esp_read_irq_enable(&s_pmu, &enable), ESP_OK, "read REG40-42");
    T_REQ("tg28_esp_set_irq_enable", API_ASPECT_NORMAL,
          tg28_esp_set_irq_enable(&s_pmu, &enable), ESP_OK, "rewrite identical IRQ mask");
    T_REQ("tg28_esp_clear_irq", API_ASPECT_NORMAL,
          tg28_esp_clear_irq(&s_pmu, &(tg28_esp_irq_t){0}), ESP_OK,
          "zero W1C mask does not clear pending flags");
    s_irq_events = 0;
    s_irq_callbacks = 0;
    T_REQ("tg28_esp_dispatch_irq", API_ASPECT_NORMAL,
          tg28_esp_dispatch_irq(&s_pmu, TG28_ESP_IRQ_ALL, irq_callback, &s_irq_events),
          ESP_OK, "read and dispatch latched flags");
    T_REQ("tg28_esp_dispatch_irq", API_ASPECT_BOUNDARY,
          tg28_esp_dispatch_irq(&s_pmu, TG28_ESP_IRQ_ALL, NULL, NULL),
          ESP_ERR_INVALID_ARG, "NULL callback");
    T_REQ("tg28_esp_update_irq_events", API_ASPECT_NORMAL,
          tg28_esp_update_irq_events(&s_pmu, TG28_ESP_IRQ_NEW_SOC,
                                      (enable.bank[2] & 0x10) != 0),
          ESP_OK, "rewrite one IRQ enable bit to current state");
    T_REQ("tg28_esp_update_irq_events", API_ASPECT_BOUNDARY,
          tg28_esp_update_irq_events(&s_pmu, 0, true), ESP_ERR_INVALID_ARG,
          "empty event mask");
    T_REQ("tg28_esp_ack_events", API_ASPECT_NORMAL,
          tg28_esp_ack_events(&s_pmu, 0), ESP_OK, "empty event ACK is a no-op");
    T_REQ("tg28_esp_ack_events", API_ASPECT_BOUNDARY,
          tg28_esp_ack_events(&s_pmu, UINT32_C(0x80000000)), ESP_ERR_INVALID_ARG,
          "unsupported IRQ bit outside TG28_ESP_IRQ_ALL");
}

static void test_charger(void)
{
    tg28_esp_charger_t charger = {0};
    uint16_t value = 0;
    T_REQ("tg28_esp_read_charger", API_ASPECT_BOUNDARY,
          tg28_esp_read_charger(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL charger");
    T_REQ("tg28_esp_read_charger", API_ASPECT_NORMAL,
          tg28_esp_read_charger(&s_pmu, &charger), ESP_OK, "decode charger settings");
    T_REQ("tg28_esp_read_charge_current", API_ASPECT_BOUNDARY,
          tg28_esp_read_charge_current(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL current");
    T_REQ("tg28_esp_read_charge_current", API_ASPECT_NORMAL,
          tg28_esp_read_charge_current(&s_pmu, &value), ESP_OK, "read CC setpoint");
    T_BOOL("tg28_esp_read_charge_current", API_ASPECT_NORMAL,
           value == charger.current_ma, "independent CC read agrees");
    T_REQ("tg28_esp_set_charge_current", API_ASPECT_BOUNDARY,
          tg28_esp_set_charge_current(&s_pmu, 201), ESP_ERR_INVALID_ARG,
          "gap between current encoding ranges");
    T_REQ("tg28_esp_set_charge_current", API_ASPECT_NORMAL,
          tg28_esp_set_charge_current(&s_pmu, charger.current_ma), ESP_OK,
          "rewrite current CC setpoint");
    T_REQ("tg28_esp_set_charger_enabled", API_ASPECT_NORMAL,
          tg28_esp_set_charger_enabled(&s_pmu, charger.enabled), ESP_OK,
          "rewrite charger enable");
    T_REQ("tg28_esp_set_input_current_limit", API_ASPECT_NORMAL,
          tg28_esp_set_input_current_limit(&s_pmu, charger.input_limit_ma), ESP_OK,
          "rewrite identical input current limit");
    T_REQ("tg28_esp_set_input_voltage_limit", API_ASPECT_NORMAL,
          tg28_esp_set_input_voltage_limit(&s_pmu, charger.input_limit_mv), ESP_OK,
          "rewrite identical input voltage limit");
    T_REQ("tg28_esp_set_precharge_current", API_ASPECT_NORMAL,
          tg28_esp_set_precharge_current(&s_pmu, charger.precharge_ma), ESP_OK,
          "rewrite identical precharge current");
    T_REQ("tg28_esp_set_charge_termination", API_ASPECT_NORMAL,
          tg28_esp_set_charge_termination(&s_pmu, charger.termination_ma,
                                          charger.termination_enabled),
          ESP_OK, "rewrite identical termination setting");
    T_REQ("tg28_esp_set_charge_voltage", API_ASPECT_NORMAL,
          tg28_esp_set_charge_voltage(&s_pmu, charger.voltage_mv), ESP_OK,
          "rewrite identical CV setpoint");
    T_REQ("tg28_esp_set_battery_detection", API_ASPECT_NORMAL,
          tg28_esp_set_battery_detection(&s_pmu, charger.detection_enabled), ESP_OK,
          "rewrite identical battery detection");

    tg28_esp_status_t status;
    T_REQ("tg28_esp_read_status", API_ASPECT_NORMAL,
          tg28_esp_read_status(&s_pmu, &status), ESP_OK, "refresh supply status");
    uint8_t soc = 0;
    esp_err_t soc_err = tg28_esp_read_soc(&s_pmu, &soc);
    if (soc_err == ESP_OK) {
        api_test_log(TAPI, "tg28_esp_read_soc", API_ASPECT_NORMAL, API_PASS,
                     "fuel gauge SOC read");
        T_BOOL("tg28_esp_read_soc", API_ASPECT_NORMAL, soc <= 100, "SOC within 0-100");
    } else {
        T_REQ("tg28_esp_read_soc", API_ASPECT_ERROR, soc_err, ESP_ERR_INVALID_STATE,
              "fuel gauge disabled or battery absent");
        T_NA("tg28_esp_read_soc", API_ASPECT_HARDWARE,
             "SOC needs an enabled gauge and active battery");
    }
    T_REQ("tg28_esp_read_soc", API_ASPECT_BOUNDARY,
          tg28_esp_read_soc(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL SOC output");

    tg28_esp_gauge_config_t gauge = {0};
    T_REQ("tg28_esp_read_gauge_config", API_ASPECT_BOUNDARY,
          tg28_esp_read_gauge_config(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL config");
    T_REQ("tg28_esp_read_gauge_config", API_ASPECT_NORMAL,
          tg28_esp_read_gauge_config(&s_pmu, &gauge), ESP_OK, "read gauge configuration");
    T_REQ("tg28_esp_set_fuel_gauge", API_ASPECT_NORMAL,
          tg28_esp_set_fuel_gauge(&s_pmu, gauge.enabled), ESP_OK,
          "rewrite identical fuel-gauge enable");

    tg28_esp_chgled_t led = {0};
    T_REQ("tg28_esp_read_chgled", API_ASPECT_BOUNDARY,
          tg28_esp_read_chgled(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL LED config");
    T_REQ("tg28_esp_read_chgled", API_ASPECT_NORMAL,
          tg28_esp_read_chgled(&s_pmu, &led), ESP_OK, "read charge LED mode");
    T_REQ("tg28_esp_set_chgled", API_ASPECT_NORMAL,
          tg28_esp_set_chgled(&s_pmu, &led), ESP_OK, "rewrite identical LED mode");

    tg28_esp_power_key_t key = {0};
    T_REQ("tg28_esp_read_power_key", API_ASPECT_BOUNDARY,
          tg28_esp_read_power_key(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL timing");
    T_REQ("tg28_esp_read_power_key", API_ASPECT_NORMAL,
          tg28_esp_read_power_key(&s_pmu, &key), ESP_OK, "read power-key timing");
    T_REQ("tg28_esp_set_power_key", API_ASPECT_NORMAL,
          tg28_esp_set_power_key(&s_pmu, &key), ESP_OK, "rewrite identical key timing");

    bool batfet_off = false;
    T_REQ("tg28_esp_read_batfet_power_off", API_ASPECT_NORMAL,
          tg28_esp_read_batfet_power_off(&s_pmu, &batfet_off), ESP_OK,
          "read BATFET off bit");
    if (!batfet_off) {
        T_REQ("tg28_esp_set_batfet_power_off", API_ASPECT_NORMAL,
              tg28_esp_set_batfet_power_off(&s_pmu, false), ESP_OK,
              "rewrite safe disabled state");
    } else {
        T_BLOCK("tg28_esp_set_batfet_power_off", API_ASPECT_HARDWARE,
                "BATFET is currently disabled and must not be disturbed");
    }
    uint16_t mv = 0;
    T_REQ("tg28_esp_read_min_system_voltage", API_ASPECT_NORMAL,
          tg28_esp_read_min_system_voltage(&s_pmu, &mv), ESP_OK, "read minimum VSYS");
    T_REQ("tg28_esp_set_min_system_voltage", API_ASPECT_NORMAL,
          tg28_esp_set_min_system_voltage(&s_pmu, mv), ESP_OK, "rewrite identical VSYS minimum");
    bool backup = false;
    T_REQ("tg28_esp_read_backup_charge", API_ASPECT_NORMAL,
          tg28_esp_read_backup_charge(&s_pmu, &backup, &mv), ESP_OK, "read backup charge");
    T_REQ("tg28_esp_set_backup_charge_enabled", API_ASPECT_NORMAL,
          tg28_esp_set_backup_charge_enabled(&s_pmu, backup), ESP_OK,
          "rewrite identical backup enable");
    T_REQ("tg28_esp_set_backup_charge_voltage", API_ASPECT_NORMAL,
          tg28_esp_set_backup_charge_voltage(&s_pmu, mv), ESP_OK,
          "rewrite identical backup voltage");

    tg28_esp_safety_timer_t timer = {0};
    T_REQ("tg28_esp_read_safety_timer", API_ASPECT_NORMAL,
          tg28_esp_read_safety_timer(&s_pmu, &timer), ESP_OK, "read safety timers");
    T_REQ("tg28_esp_set_safety_timer", API_ASPECT_NORMAL,
          tg28_esp_set_safety_timer(&s_pmu, &timer), ESP_OK, "rewrite identical safety timers");
    T_BLOCK("tg28_esp_reset_fuel_gauge", API_ASPECT_HARDWARE,
            "fuel-gauge reset changes battery learning state");
    T_BLOCK("tg28_esp_request_power", API_ASPECT_HARDWARE,
            "power-off/restart removes the test host");
}

static void test_regulators(void)
{
    bool enabled = false;
    uint16_t mv = 0;
    for (tg28_esp_rail_t rail = TG28_ESP_RAIL_DCDC1; rail <= TG28_ESP_RAIL_DC4SW; ++rail) {
        const esp_err_t read_enable = tg28_esp_read_rail_enabled(&s_pmu, rail, &enabled);
        T_REQ("tg28_esp_read_rail_enabled", API_ASPECT_NORMAL, read_enable, ESP_OK,
              "read rail enable bit");
        if (read_enable == ESP_OK) {
            T_REQ("tg28_esp_set_rail_enabled", API_ASPECT_NORMAL,
                  tg28_esp_set_rail_enabled(&s_pmu, rail, enabled), ESP_OK,
                  "rewrite identical rail enable");
        }
        const esp_err_t read_voltage = tg28_esp_read_rail_voltage(&s_pmu, rail, &mv);
        if (read_voltage == ESP_OK) {
            api_test_log(TAPI, "tg28_esp_read_rail_voltage", API_ASPECT_NORMAL, API_PASS,
                         "read programmed rail setpoint");
            const esp_err_t set_voltage = tg28_esp_set_rail_voltage(&s_pmu, rail, mv);
            if (set_voltage == ESP_OK) {
                api_test_log(TAPI, "tg28_esp_set_rail_voltage", API_ASPECT_NORMAL, API_PASS,
                             "rewrite identical rail setpoint");
            } else if (set_voltage == ESP_ERR_NOT_SUPPORTED) {
                api_test_log(TAPI, "tg28_esp_set_rail_voltage", API_ASPECT_NORMAL, API_PASS,
                             "switch rail follows upstream DCDC as documented");
            } else {
                api_test_require(TAPI, "tg28_esp_set_rail_voltage", API_ASPECT_NORMAL,
                                 set_voltage, ESP_OK, "rewrite rail setpoint");
            }
        } else {
            api_test_require(TAPI, "tg28_esp_read_rail_voltage", API_ASPECT_ERROR,
                             read_voltage, ESP_ERR_INVALID_RESPONSE,
                             "reserved rail encoding reported");
            T_BLOCK("tg28_esp_read_rail_voltage", API_ASPECT_HARDWARE,
                    "rail uses reserved encoding");
            T_BLOCK("tg28_esp_set_rail_voltage", API_ASPECT_HARDWARE,
                    "rail uses reserved encoding");
        }
    }
    T_REQ("tg28_esp_read_rail_enabled", API_ASPECT_BOUNDARY,
          tg28_esp_read_rail_enabled(&s_pmu, (tg28_esp_rail_t)99, &enabled),
          ESP_ERR_INVALID_ARG, "invalid rail enum");
    T_REQ("tg28_esp_set_rail_enabled", API_ASPECT_BOUNDARY,
          tg28_esp_set_rail_enabled(&s_pmu, (tg28_esp_rail_t)99, false),
          ESP_ERR_INVALID_ARG, "invalid rail enum");
    T_REQ("tg28_esp_read_rail_voltage", API_ASPECT_BOUNDARY,
          tg28_esp_read_rail_voltage(&s_pmu, TG28_ESP_RAIL_DCDC1, NULL),
          ESP_ERR_INVALID_ARG, "NULL voltage output");
    T_REQ("tg28_esp_set_rail_voltage", API_ASPECT_BOUNDARY,
          tg28_esp_set_rail_voltage(&s_pmu, TG28_ESP_RAIL_DCDC1, 1499),
          ESP_ERR_INVALID_ARG, "voltage below DCDC1 range");
    T_REQ("tg28_esp_set_rail_voltage", API_ASPECT_ERROR,
          tg28_esp_set_rail_voltage(&s_pmu, TG28_ESP_RAIL_DC1SW, 3300),
          ESP_ERR_NOT_SUPPORTED, "switch rail cannot set voltage");

    bool forced = false;
    T_REQ("tg28_esp_read_dcdc_pwm", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_pwm(&s_pmu, TG28_ESP_RAIL_DCDC2, &forced), ESP_OK,
          "read forced-PWM bit");
    T_REQ("tg28_esp_set_dcdc_pwm", API_ASPECT_NORMAL,
          tg28_esp_set_dcdc_pwm(&s_pmu, TG28_ESP_RAIL_DCDC2, forced), ESP_OK,
          "rewrite identical forced-PWM bit");
    T_REQ("tg28_esp_read_dcdc_pwm", API_ASPECT_BOUNDARY,
          tg28_esp_read_dcdc_pwm(&s_pmu, TG28_ESP_RAIL_ALDO1, &forced),
          ESP_ERR_INVALID_ARG, "PWM read only applies to DCDC1-4");

    uint8_t adc_mask = 0;
    T_REQ("tg28_esp_read_adc_mask", API_ASPECT_NORMAL,
          tg28_esp_read_adc_mask(&s_pmu, &adc_mask), ESP_OK, "read ADC enable mask");
    T_REQ("tg28_esp_set_adc_channels", API_ASPECT_BOUNDARY,
          tg28_esp_set_adc_channels(&s_pmu, 0x80, true), ESP_ERR_INVALID_ARG,
          "unsupported ADC channel bit");
    T_REQ("tg28_esp_set_adc_channels", API_ASPECT_NORMAL,
          tg28_esp_set_adc_channels(&s_pmu, 0, false), ESP_OK, "zero ADC mask is no-op");
    uint16_t raw = 0;
    uint32_t uv = 0;
    for (tg28_esp_adc_channel_t channel = TG28_ESP_ADC_VBAT;
         channel <= TG28_ESP_ADC_GP; channel <<= 1) {
        if (channel == TG28_ESP_ADC_GP) {
            T_REQ("tg28_esp_read_adc_raw", API_ASPECT_ERROR,
                  tg28_esp_read_adc_raw(&s_pmu, channel, &raw), ESP_ERR_NOT_SUPPORTED,
                  "GP ADC has no documented sample register");
            continue;
        }
        if (!(adc_mask & channel)) {
            T_REQ("tg28_esp_read_adc_raw", API_ASPECT_ERROR,
                  tg28_esp_read_adc_raw(&s_pmu, channel, &raw), ESP_ERR_INVALID_STATE,
                  "disabled ADC channel rejected");
            T_REQ("tg28_esp_read_adc_uv", API_ASPECT_ERROR,
                  tg28_esp_read_adc_uv(&s_pmu, channel, &uv), ESP_ERR_INVALID_STATE,
                  "disabled ADC channel rejected");
            continue;
        }
        const esp_err_t read_raw = tg28_esp_read_adc_raw(&s_pmu, channel, &raw);
        api_test_require(TAPI, "tg28_esp_read_adc_raw", API_ASPECT_NORMAL, read_raw,
                         ESP_OK, "read enabled ADC raw sample");
        if (read_raw == ESP_OK) {
            const esp_err_t read_uv = tg28_esp_read_adc_uv(&s_pmu, channel, &uv);
            if (read_uv == ESP_OK) {
                api_test_log(TAPI, "tg28_esp_read_adc_uv", API_ASPECT_NORMAL, API_PASS,
                             "convert enabled ADC sample");
            } else {
                api_test_require(TAPI, "tg28_esp_read_adc_uv", API_ASPECT_ERROR,
                                 read_uv, ESP_ERR_INVALID_RESPONSE,
                                 "TS overflow reported");
            }
        }
    }
    T_REQ("tg28_esp_read_adc_raw", API_ASPECT_BOUNDARY,
          tg28_esp_read_adc_raw(&s_pmu, (tg28_esp_adc_channel_t)0x40, &raw),
          ESP_ERR_INVALID_ARG, "invalid ADC channel");
    T_REQ("tg28_esp_read_adc_uv", API_ASPECT_BOUNDARY,
          tg28_esp_read_adc_uv(&s_pmu, TG28_ESP_ADC_VBAT, NULL),
          ESP_ERR_INVALID_ARG, "NULL voltage output");

    int32_t millicelsius = 0;
    T_REQ("tg28_esp_read_die_temperature", API_ASPECT_BOUNDARY,
          tg28_esp_read_die_temperature(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL output");
    esp_err_t die_err = tg28_esp_read_die_temperature(&s_pmu, &millicelsius);
    if (die_err == ESP_OK) {
        api_test_check_bool(TAPI, "tg28_esp_read_die_temperature", API_ASPECT_HARDWARE,
                            millicelsius > -60000 && millicelsius < 160000,
                            "die temperature in plausible operating range");
    } else {
        T_REQ("tg28_esp_read_die_temperature", API_ASPECT_ERROR, die_err,
              ESP_ERR_INVALID_STATE, "DIE ADC is disabled");
        T_NA("tg28_esp_read_die_temperature", API_ASPECT_HARDWARE,
             "DIE ADC sample unavailable");
    }
}

static void test_power_control(void)
{
    tg28_esp_power_control_t power = {0};
    tg28_esp_shutdown_control_t shutdown = {0};
    bool ovp = false;
    uint8_t uvp = 0;
    uint16_t mv = 0;

    T_REQ("tg28_esp_read_power_control", API_ASPECT_BOUNDARY,
          tg28_esp_read_power_control(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL control");
    T_REQ("tg28_esp_read_power_control", API_ASPECT_NORMAL,
          tg28_esp_read_power_control(&s_pmu, &power), ESP_OK, "read REG10 control bits");
    T_REQ("tg28_esp_set_power_control", API_ASPECT_NORMAL,
          tg28_esp_set_power_control(&s_pmu, &power), ESP_OK, "rewrite identical REG10 bits");
    T_REQ("tg28_esp_read_shutdown_control", API_ASPECT_BOUNDARY,
          tg28_esp_read_shutdown_control(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL control");
    T_REQ("tg28_esp_read_shutdown_control", API_ASPECT_NORMAL,
          tg28_esp_read_shutdown_control(&s_pmu, &shutdown), ESP_OK, "read shutdown policy");
    T_REQ("tg28_esp_set_shutdown_control", API_ASPECT_NORMAL,
          tg28_esp_set_shutdown_control(&s_pmu, &shutdown), ESP_OK,
          "rewrite identical shutdown policy");

    T_REQ("tg28_esp_read_dcdc_protection", API_ASPECT_BOUNDARY,
          tg28_esp_read_dcdc_protection(&s_pmu, NULL, &uvp), ESP_ERR_INVALID_ARG,
          "NULL OVP output");
    T_REQ("tg28_esp_read_dcdc_protection", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_protection(&s_pmu, &ovp, &uvp), ESP_OK,
          "read OVP/UVP protection");
    T_REQ("tg28_esp_set_dcdc_protection", API_ASPECT_BOUNDARY,
          tg28_esp_set_dcdc_protection(&s_pmu, ovp, 16), ESP_ERR_INVALID_ARG,
          "UVP mask above 4 bits");
    T_REQ("tg28_esp_set_dcdc_protection", API_ASPECT_NORMAL,
          tg28_esp_set_dcdc_protection(&s_pmu, ovp, uvp), ESP_OK,
          "rewrite identical protection");

    T_REQ("tg28_esp_read_shutdown_voltage", API_ASPECT_NORMAL,
          tg28_esp_read_shutdown_voltage(&s_pmu, &mv), ESP_OK, "read shutdown voltage");
    T_REQ("tg28_esp_set_shutdown_voltage", API_ASPECT_NORMAL,
          tg28_esp_set_shutdown_voltage(&s_pmu, mv), ESP_OK,
          "rewrite identical shutdown voltage");

    tg28_esp_pwrok_t pwrok = {0};
    T_REQ("tg28_esp_read_pwrok", API_ASPECT_NORMAL,
          tg28_esp_read_pwrok(&s_pmu, &pwrok), ESP_OK, "read PWR_OK timing");
    T_REQ("tg28_esp_set_pwrok", API_ASPECT_NORMAL,
          tg28_esp_set_pwrok(&s_pmu, &pwrok), ESP_OK, "rewrite identical PWR_OK timing");

    uint8_t slot = 0;
    for (tg28_esp_rail_t rail = TG28_ESP_RAIL_DCDC1; rail <= TG28_ESP_RAIL_DC4SW; ++rail) {
        const esp_err_t read = tg28_esp_read_fast_sequence(&s_pmu, rail, &slot);
        api_test_require(TAPI, "tg28_esp_read_fast_sequence", API_ASPECT_NORMAL,
                         read, ESP_OK, "read fast-start slot");
        if (read == ESP_OK) {
            T_REQ("tg28_esp_set_fast_sequence", API_ASPECT_NORMAL,
                  tg28_esp_set_fast_sequence(&s_pmu, rail, slot), ESP_OK,
                  "rewrite identical fast-start slot");
        }
    }
    T_REQ("tg28_esp_read_fast_sequence", API_ASPECT_BOUNDARY,
          tg28_esp_read_fast_sequence(&s_pmu, (tg28_esp_rail_t)99, &slot),
          ESP_ERR_INVALID_ARG, "invalid rail enum");

    bool fast_on = false, fast_wake = false;
    T_REQ("tg28_esp_read_fast_start", API_ASPECT_NORMAL,
          tg28_esp_read_fast_start(&s_pmu, &fast_on, &fast_wake), ESP_OK,
          "read fast-start flags");
    T_REQ("tg28_esp_set_fast_start", API_ASPECT_NORMAL,
          tg28_esp_set_fast_start(&s_pmu, fast_on, fast_wake), ESP_OK,
          "rewrite identical fast-start flags");
}

static void test_protected_regulators(void)
{
    bool dvm = false;
    T_REQ("tg28_esp_read_dvm", API_ASPECT_NORMAL,
          tg28_esp_read_dvm(&s_pmu, TG28_ESP_RAIL_DCDC2, &dvm), ESP_OK,
          "read DCDC2 DVM bit");
    T_REQ("tg28_esp_set_dvm", API_ASPECT_NORMAL,
          tg28_esp_set_dvm(&s_pmu, TG28_ESP_RAIL_DCDC2, dvm), ESP_OK,
          "rewrite identical DVM bit");
    T_REQ("tg28_esp_read_dvm", API_ASPECT_BOUNDARY,
          tg28_esp_read_dvm(&s_pmu, TG28_ESP_RAIL_DCDC1, &dvm),
          ESP_ERR_INVALID_ARG, "DVM only supported on DCDC2/3");

    bool ccm = false, slow = false;
    T_REQ("tg28_esp_read_dcdc_control", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_control(&s_pmu, &ccm, &slow), ESP_OK,
          "read DCDC CCM/slow-ramp");
    T_REQ("tg28_esp_set_dcdc_control", API_ASPECT_NORMAL,
          tg28_esp_set_dcdc_control(&s_pmu, ccm, slow), ESP_OK,
          "rewrite identical DCDC control");

    bool spread = false;
    uint8_t khz = 0;
    T_REQ("tg28_esp_read_dcdc_spread", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_spread(&s_pmu, &spread, &khz), ESP_OK,
          "read DCDC spread spectrum");
    T_REQ("tg28_esp_set_dcdc_spread", API_ASPECT_BOUNDARY,
          tg28_esp_set_dcdc_spread(&s_pmu, spread, 75), ESP_ERR_INVALID_ARG,
          "only 50 or 100 kHz accepted");
    T_REQ("tg28_esp_set_dcdc_spread", API_ASPECT_NORMAL,
          tg28_esp_set_dcdc_spread(&s_pmu, spread, khz), ESP_OK,
          "rewrite identical spread spectrum");

    uint16_t delay_us = 0;
    T_REQ("tg28_esp_read_dcdc_uvp_delay", API_ASPECT_NORMAL,
          tg28_esp_read_dcdc_uvp_delay(&s_pmu, &delay_us), ESP_OK,
          "read DCDC UVP delay");
    T_REQ("tg28_esp_set_dcdc_uvp_delay", API_ASPECT_BOUNDARY,
          tg28_esp_set_dcdc_uvp_delay(&s_pmu, 61), ESP_ERR_INVALID_ARG,
          "delay must be a 60 us step");
    T_REQ("tg28_esp_set_dcdc_uvp_delay", API_ASPECT_NORMAL,
          tg28_esp_set_dcdc_uvp_delay(&s_pmu, delay_us), ESP_OK,
          "rewrite identical UVP delay");
}

static void test_thermal(void)
{
    bool enabled = false;
    uint8_t celsius = 0;
    T_REQ("tg28_esp_read_die_protection", API_ASPECT_NORMAL,
          tg28_esp_read_die_protection(&s_pmu, &enabled, &celsius), ESP_OK,
          "read die protection");
    T_REQ("tg28_esp_set_die_protection", API_ASPECT_NORMAL,
          tg28_esp_set_die_protection(&s_pmu, enabled, celsius), ESP_OK,
          "rewrite identical die protection");
    T_REQ("tg28_esp_read_charge_thermal_limit", API_ASPECT_NORMAL,
          tg28_esp_read_charge_thermal_limit(&s_pmu, &celsius), ESP_OK,
          "read charge thermal limit");
    T_REQ("tg28_esp_set_charge_thermal_limit", API_ASPECT_NORMAL,
          tg28_esp_set_charge_thermal_limit(&s_pmu, celsius), ESP_OK,
          "rewrite identical charge thermal limit");

    tg28_esp_ts_t ts = {0};
    T_REQ("tg28_esp_read_ts", API_ASPECT_NORMAL,
          tg28_esp_read_ts(&s_pmu, &ts), ESP_OK, "read TS bias configuration");
    T_REQ("tg28_esp_set_ts", API_ASPECT_NORMAL,
          tg28_esp_set_ts(&s_pmu, &ts), ESP_OK, "rewrite identical TS configuration");
    uint32_t override_uv = 0;
    T_REQ("tg28_esp_read_ts_override_uv", API_ASPECT_NORMAL,
          tg28_esp_read_ts_override_uv(&s_pmu, &override_uv), ESP_OK,
          "read TS ADC override voltage");
    const tg28_esp_ts_point_t curve[] = {{400000, 30000}, {600000, 20000}};
    int32_t temp_mc = 0;
    esp_err_t temp_err = tg28_esp_read_ts_temperature(&s_pmu, curve, 2, &temp_mc);
    if (temp_err == ESP_OK) {
        T_BOOL("tg28_esp_read_ts_temperature", API_ASPECT_HARDWARE,
               temp_mc > -60000 && temp_mc < 160000, "board curve interpolated in range");
    } else {
        T_REQ("tg28_esp_read_ts_temperature", API_ASPECT_ERROR, temp_err,
              ESP_ERR_INVALID_RESPONSE, "outside uncalibrated test curve");
        T_BLOCK("tg28_esp_read_ts_temperature", API_ASPECT_HARDWARE,
                "fitted TS curve is not available in the BSP");
    }
    T_REQ("tg28_esp_read_ts_temperature", API_ASPECT_BOUNDARY,
          tg28_esp_read_ts_temperature(&s_pmu, curve, 1, &temp_mc),
          ESP_ERR_INVALID_ARG, "curve needs at least two points");

    uint16_t threshold_mv = 0;
    for (tg28_esp_ts_threshold_t threshold = TG28_ESP_TS_COLD_HYSTERESIS;
         threshold <= TG28_ESP_TS_JEITA_WARM; ++threshold) {
        const esp_err_t read = tg28_esp_read_ts_threshold(&s_pmu, threshold, &threshold_mv);
        api_test_require(TAPI, "tg28_esp_read_ts_threshold", API_ASPECT_NORMAL,
                         read, ESP_OK, "read TS threshold voltage");
        if (read == ESP_OK) {
            T_REQ("tg28_esp_set_ts_threshold", API_ASPECT_NORMAL,
                  tg28_esp_set_ts_threshold(&s_pmu, threshold, threshold_mv), ESP_OK,
                  "rewrite identical TS threshold");
        }
    }
    T_REQ("tg28_esp_read_ts_threshold", API_ASPECT_BOUNDARY,
          tg28_esp_read_ts_threshold(&s_pmu, (tg28_esp_ts_threshold_t)99, &threshold_mv),
          ESP_ERR_INVALID_ARG, "invalid threshold enum");

    bool jeita = false;
    T_REQ("tg28_esp_read_jeita_enabled", API_ASPECT_NORMAL,
          tg28_esp_read_jeita_enabled(&s_pmu, &jeita), ESP_OK, "read JEITA enable");
    T_REQ("tg28_esp_set_jeita_enabled", API_ASPECT_NORMAL,
          tg28_esp_set_jeita_enabled(&s_pmu, jeita), ESP_OK,
          "rewrite identical JEITA enable");
    tg28_esp_jeita_t derating = {0};
    T_REQ("tg28_esp_read_jeita", API_ASPECT_NORMAL,
          tg28_esp_read_jeita(&s_pmu, &derating), ESP_OK, "read JEITA derating");
    T_REQ("tg28_esp_set_jeita", API_ASPECT_NORMAL,
          tg28_esp_set_jeita(&s_pmu, &derating), ESP_OK,
          "rewrite identical JEITA derating");
}

static void test_sleep_watchdog_soc(void)
{
    tg28_esp_sleep_t sleep = {0};
    T_REQ("tg28_esp_read_sleep_config", API_ASPECT_NORMAL,
          tg28_esp_read_sleep_config(&s_pmu, &sleep), ESP_OK, "read REG26 sleep options");
    bool sleeping = false, waking = false;
    T_REQ("tg28_esp_read_sleep_controls", API_ASPECT_NORMAL,
          tg28_esp_read_sleep_controls(&s_pmu, &sleeping, &waking), ESP_OK,
          "read latched sleep/wake controls");
    T_REQ("tg28_esp_enter_sleep", API_ASPECT_BOUNDARY,
          tg28_esp_enter_sleep(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL sleep config");
    T_BLOCK("tg28_esp_enter_sleep", API_ASPECT_HARDWARE,
            "actual sleep command interrupts the USB test session");
    T_REQ("tg28_esp_request_wakeup", API_ASPECT_BOUNDARY,
          tg28_esp_request_wakeup(&s_pmu, NULL), ESP_ERR_INVALID_ARG, "NULL sleep config");
    if (!sleeping) {
        T_REQ("tg28_esp_request_wakeup", API_ASPECT_ERROR,
              tg28_esp_request_wakeup(&s_pmu, &sleep), ESP_ERR_INVALID_STATE,
              "wake rejected while SLEEP_EN is clear");
    } else {
        T_BLOCK("tg28_esp_request_wakeup", API_ASPECT_HARDWARE,
                "SLEEP_EN already asserted; automated wake would alter PMU state");
    }

    bool watchdog = false;
    uint16_t seconds = 0;
    tg28_esp_watchdog_action_t action = TG28_ESP_WATCHDOG_IRQ;
    T_REQ("tg28_esp_read_watchdog", API_ASPECT_BOUNDARY,
          tg28_esp_read_watchdog(&s_pmu, NULL, &seconds, &action),
          ESP_ERR_INVALID_ARG, "NULL watchdog output");
    T_REQ("tg28_esp_read_watchdog", API_ASPECT_NORMAL,
          tg28_esp_read_watchdog(&s_pmu, &watchdog, &seconds, &action), ESP_OK,
          "read watchdog enable/period/action");
    T_REQ("tg28_esp_configure_watchdog", API_ASPECT_BOUNDARY,
          tg28_esp_configure_watchdog(&s_pmu, 3, action), ESP_ERR_INVALID_ARG,
          "period must be a power of two");
    if (!watchdog) {
        T_REQ("tg28_esp_configure_watchdog", API_ASPECT_NORMAL,
              tg28_esp_configure_watchdog(&s_pmu, seconds, action), ESP_OK,
              "rewrite identical disabled watchdog setup");
        T_REQ("tg28_esp_feed_watchdog", API_ASPECT_NORMAL,
              tg28_esp_feed_watchdog(&s_pmu), ESP_OK, "feed while disabled");
        T_REQ("tg28_esp_set_watchdog_enabled", API_ASPECT_NORMAL,
              tg28_esp_set_watchdog_enabled(&s_pmu, false), ESP_OK,
              "keep watchdog disabled");
    } else {
        T_BLOCK("tg28_esp_configure_watchdog", API_ASPECT_HARDWARE,
                "watchdog is active; timing is left untouched");
        T_REQ("tg28_esp_feed_watchdog", API_ASPECT_NORMAL,
              tg28_esp_feed_watchdog(&s_pmu), ESP_OK, "feed active watchdog");
        T_BLOCK("tg28_esp_set_watchdog_enabled", API_ASPECT_HARDWARE,
                "active watchdog action could reset or power-cycle the host");
    }

    uint8_t level1 = 0, level2 = 0;
    T_REQ("tg28_esp_read_soc_thresholds", API_ASPECT_NORMAL,
          tg28_esp_read_soc_thresholds(&s_pmu, &level1, &level2), ESP_OK,
          "read SOC warning thresholds");
    T_REQ("tg28_esp_set_soc_thresholds", API_ASPECT_NORMAL,
          tg28_esp_set_soc_thresholds(&s_pmu, level1, level2), ESP_OK,
          "rewrite identical SOC thresholds");
    T_REQ("tg28_esp_set_soc_thresholds", API_ASPECT_BOUNDARY,
          tg28_esp_set_soc_thresholds(&s_pmu, 16, 5), ESP_ERR_INVALID_ARG,
          "level 1 above 15 percent rejected");
    T_REQ("tg28_esp_request_power", API_ASPECT_BOUNDARY,
          tg28_esp_request_power(&s_pmu, (tg28_esp_power_command_t)2),
          ESP_ERR_INVALID_ARG, "invalid power command enum");
}

void api_tests_tg28(void)
{
    api_test_suite_begin(TAPI);
    test_identity();
    if (!tg28_open_common(true)) {
        T_BLOCK("tg28_esp_open", API_ASPECT_HARDWARE,
                "TG28-ESP did not accept the core I2C device");
        api_test_suite_end(TAPI);
        return;
    }
    api_test_log(TAPI, "tg28_esp_open", API_ASPECT_NORMAL, API_PASS,
                 "global test context attached with protection opt-in");
    test_basic_reads_writes();
    test_irq();
    test_charger();
    test_regulators();
    test_power_control();
    test_protected_regulators();
    test_thermal();
    test_sleep_watchdog_soc();
    T_REQ("tg28_esp_close", API_ASPECT_LIFECYCLE, tg28_esp_close(&s_pmu), ESP_OK,
          "release global TG28 test context");
    api_test_suite_end(TAPI);
}
