/* SPDX-License-Identifier: Apache-2.0 */
#include <stdbool.h>
#include <stdint.h>

#include "ulp_lp_core_i2c.h"
#include "ulp_lp_core_utils.h"

#define TG28_7BIT_ADDRESS 0x34
#define TG28_CHIP_ID_REGISTER 0x03
#define TG28_CHIP_ID 0x4A
#define LP_I2C_TIMEOUT_CYCLES 5000
#define HP_SLEEP_SETTLE_US 500000

volatile uint32_t lp_tg28_chip_id;
volatile uint32_t lp_tg28_attempts;
volatile int32_t lp_tg28_last_error;
volatile bool lp_tg28_transaction_ok;

int main(void)
{
    /* Allow the HP core to enter deep sleep before raising its wake interrupt. */
    ulp_lp_core_delay_us(HP_SLEEP_SETTLE_US);
    for (;;) {
        const uint8_t register_address = TG28_CHIP_ID_REGISTER;
        uint8_t chip_id = 0;
        ++lp_tg28_attempts;
        esp_err_t err = lp_core_i2c_master_write_to_device(
            LP_I2C_NUM_0, TG28_7BIT_ADDRESS, &register_address,
            sizeof(register_address), LP_I2C_TIMEOUT_CYCLES);
        if (err == ESP_OK) {
            err = lp_core_i2c_master_read_from_device(
                LP_I2C_NUM_0, TG28_7BIT_ADDRESS, &chip_id,
                sizeof(chip_id), LP_I2C_TIMEOUT_CYCLES);
        }
        lp_tg28_last_error = err;
        if (err == ESP_OK) {
            lp_tg28_chip_id = chip_id;
            lp_tg28_transaction_ok = chip_id == TG28_CHIP_ID;
            ulp_lp_core_wakeup_main_processor();
            return 0;
        }
        ulp_lp_core_delay_us(100000);
    }
}
