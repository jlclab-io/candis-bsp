/* SPDX-License-Identifier: Apache-2.0 */
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG_DOWNLOAD
#include <stdatomic.h>
#include <stdlib.h>
#include "candis_usb_download.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/usb_utmi_ll.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#endif

static const char *TAG = "candis_otg";
static const tinyusb_cdcacm_itf_t PORT = TINYUSB_CDC_ACM_0;

#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG_DOWNLOAD
static candis_usb_download_sequence_t s_sequence;
static atomic_bool s_download_requested;

static void on_device_event(tinyusb_event_t *event, void *arg)
{
    if (event->id == TINYUSB_EVENT_ATTACHED || event->id == TINYUSB_EVENT_DETACHED) {
        s_sequence = (candis_usb_download_sequence_t){0};
    }
}

static void on_line_state(int interface, cdcacm_event_t *event)
{
    if (interface == PORT && event->type == CDC_EVENT_LINE_STATE_CHANGED &&
            candis_usb_download_sequence(&s_sequence, event->line_state_changed_data.dtr,
                                         event->line_state_changed_data.rts, esp_timer_get_time())) {
        atomic_store(&s_download_requested, true);
    }
}

static esp_err_t enter_download(void)
{
    /* Give the host CDC control request time to complete before detaching. */
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_deinit(PORT), TAG, "release CDC");
    ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "release USB OTG device");
    usb_utmi_ll_reset_register();
    usb_utmi_ll_enable_bus_clock(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
    /* Reset the S31 digital domain while preserving the LP download flag. */
    esp_rom_software_reset_system();
    abort();
}
#endif

static void echo_task(void *arg)
{
    uint8_t buffer[64];
    for (;;) {
#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG_DOWNLOAD
        if (atomic_exchange(&s_download_requested, false)) {
            ESP_LOGI(TAG, "OTG switching from application CDC to ROM");
            ESP_ERROR_CHECK_WITHOUT_ABORT(enter_download());
            vTaskDelete(NULL);
        }
#endif
        size_t count = 0;
        esp_err_t err = tinyusb_cdcacm_read(PORT, buffer, sizeof(buffer), &count);
        if (err == ESP_OK && count) {
            size_t queued = tinyusb_cdcacm_write_queue(PORT, buffer, count);
            err = tinyusb_cdcacm_write_flush(PORT, pdMS_TO_TICKS(100));
            if (queued != count || err != ESP_OK) {
                ESP_LOGW(TAG, "CDC echo incomplete: RX=%u TX=%u status=%s",
                         (unsigned)count, (unsigned)queued, esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

esp_err_t candis_diagnostics_start_otg(void)
{
#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG_DOWNLOAD
    const tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG(on_device_event);
    const tinyusb_config_cdcacm_t cdc_config = {.cdc_port = PORT, .callback_line_state_changed = on_line_state};
#else
    const tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    const tinyusb_config_cdcacm_t cdc_config = {.cdc_port = PORT};
#endif
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&usb_config), TAG, "install USB device stack");
    esp_err_t err = tinyusb_cdcacm_init(&cdc_config);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_driver_uninstall());
        return err;
    }
    if (xTaskCreate(echo_task, "candis_otg", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_cdcacm_deinit(PORT));
        ESP_ERROR_CHECK_WITHOUT_ABORT(tinyusb_driver_uninstall());
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTG CDC echo ready");
    return ESP_OK;
}
