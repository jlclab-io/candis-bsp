/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t data[8];
    gpio_num_t pclk;
    gpio_num_t vsync;
    gpio_num_t hsync;
    gpio_num_t reset;                  /**< Active low. Use GPIO_NUM_NC if wired externally. */
    gpio_num_t pwdn;                   /**< Active high. Use GPIO_NUM_NC if wired externally. */
    bool pwdn_open_drain;
    gpio_num_t xclk;                   /**< GPIO_NUM_NC for an externally supplied clock. */
    uint32_t xclk_hz;
    ledc_timer_t xclk_timer;
    ledc_channel_t xclk_channel;
    i2c_master_bus_handle_t sccb_bus;  /**< An optional borrowed handle. */
    i2c_port_num_t sccb_port;          /**< Use when ESP Video owns a dedicated bus. */
    gpio_num_t sccb_scl;               /**< GPIO_NUM_NC when using a borrowed bus. */
    gpio_num_t sccb_sda;               /**< GPIO_NUM_NC when using a borrowed bus. */
    uint32_t sccb_hz;
    /** Optional resource callbacks. With SCCB pins set to GPIO_NUM_NC, acquire
     * may supply a shared SCCB handle. Apply the board's supply order and
     * settling delays before returning.
     * Release is called after any acquire attempt, including partial failures.
     * Keep context and borrowed resources valid until stop succeeds.
     */
    esp_err_t (*acquire)(void *context, i2c_master_bus_handle_t *sccb_bus);
    esp_err_t (*release)(void *context);
    void *context;
} candis_ov5640_config_t;

/** Start one configured DVP instance. Call lifecycle APIs from one control task.
 * Reserve the configured pins, LEDC resources and ESP Video DVP instance for
 * this component. Serialize ESP Video lifecycle calls in the application.
 * On a cleanup failure, call stop again before retrying start.
 */
esp_err_t candis_ov5640_start_with_config(const candis_ov5640_config_t *config);

#ifdef CONFIG_CANDIS_OV5640_FIXTURE
/** Fill the configuration for Candis and the validation fixture. */
candis_ov5640_config_t candis_ov5640_fixture_config(void);
/** Start with the validation fixture configuration. */
esp_err_t candis_ov5640_start(void);
#endif
/** V4L2 device path. Available after successful start. */
const char *candis_ov5640_device(void);
/** Stop streaming and close application file descriptors before calling.
 * Safe to call again after successful stop.
 */
esp_err_t candis_ov5640_stop(void);

#ifdef __cplusplus
}
#endif
