/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_usb.h"
#include "candis_internal.h"
#include "sdkconfig.h"

#if CONFIG_CANDIS_USB_DEVICE || CONFIG_CANDIS_USB_HOST
#include "esp_check.h"
static const char *TAG = "candis_usb";
typedef enum {
    USB_IDLE,
    USB_DEVICE,
    USB_HOST,
} usb_role_t;
static usb_role_t s_role;
#endif

#if CONFIG_CANDIS_USB_DEVICE
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tusb.h"

static const tinyusb_cdcacm_itf_t PORT = TINYUSB_CDC_ACM_0;
static struct {
    bool driver_installed;
    bool cdc_initialized;
    bool ready;
} s_device;

static esp_err_t release_device(void)
{
    s_device.ready = false;
    /* TinyUSB callbacks can still access the ACM object until its task stops. */
    if (s_device.driver_installed) {
        ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "release device stack");
        s_device.driver_installed = false;
    }
    if (s_device.cdc_initialized) {
        ESP_RETURN_ON_ERROR(tinyusb_cdcacm_deinit(PORT), TAG, "release CDC");
        s_device.cdc_initialized = false;
    }
    s_role = USB_IDLE;
    return ESP_OK;
}
#endif

esp_err_t candis_usb_device_start(void)
{
#if CONFIG_CANDIS_USB_DEVICE
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_role == USB_IDLE) {
        const tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
        err = tinyusb_driver_install(&usb);
        if (err == ESP_OK) {
            s_role = USB_DEVICE;
            s_device.driver_installed = true;
            const tinyusb_config_cdcacm_t cdc = {.cdc_port = PORT};
            err = tinyusb_cdcacm_init(&cdc);
            s_device.cdc_initialized = err == ESP_OK;
            s_device.ready = s_device.cdc_initialized;
            if (err != ESP_OK) {
                ESP_ERROR_CHECK_WITHOUT_ABORT(release_device());
            }
        }
    } else if (s_role == USB_DEVICE && s_device.ready) {
        err = ESP_OK;
    }
    candis_control_leave();
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t candis_usb_device_stop(void)
{
#if CONFIG_CANDIS_USB_DEVICE
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_role == USB_DEVICE ? release_device() : ESP_OK;
    candis_control_leave();
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t candis_usb_device_read(void *buffer, size_t capacity, size_t *received)
{
    if (!buffer || !capacity || !received) {
        return ESP_ERR_INVALID_ARG;
    }
    *received = 0;
#if CONFIG_CANDIS_USB_DEVICE
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_role == USB_DEVICE && s_device.ready
                          ? tinyusb_cdcacm_read(PORT, buffer, capacity, received) : ESP_ERR_INVALID_STATE;
    candis_control_leave();
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t candis_usb_device_write(const void *buffer, size_t bytes, size_t *queued)
{
    if (!buffer || !bytes || !queued) {
        return ESP_ERR_INVALID_ARG;
    }
    *queued = 0;
#if CONFIG_CANDIS_USB_DEVICE
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_role == USB_DEVICE && s_device.ready && tud_cdc_n_connected(PORT)) {
        *queued = tinyusb_cdcacm_write_queue(PORT, buffer, bytes);
        err = tinyusb_cdcacm_write_flush(PORT, 0);
        if (err == ESP_OK && !*queued) {
            err = ESP_ERR_TIMEOUT;
        }
    }
    candis_control_leave();
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_CANDIS_USB_HOST
#include <stdatomic.h>
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

static struct {
    TaskHandle_t task;
    SemaphoreHandle_t done;
    atomic_bool stopping;
    bool ready;
} s_host;

static void host_worker(void *arg)
{
    SemaphoreHandle_t done = arg;
    for (;;) {
        uint32_t events = 0;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(25) + 1, &events);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "host event service: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(25) + 1);
        }
        if (events & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            err = usb_host_device_free_all();
            if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
                ESP_LOGW(TAG, "free host devices: %s", esp_err_to_name(err));
            }
        }
        if (atomic_load(&s_host.stopping)) {
            usb_host_lib_info_t info;
            if (usb_host_lib_info(&info) == ESP_OK && !info.num_clients && !info.num_devices &&
                usb_host_uninstall() == ESP_OK) {
                xSemaphoreGive(done);
                vTaskDelete(NULL);
                return;
            }
        }
    }
}

static esp_err_t start_host(void)
{
    s_host.done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_host.done, ESP_ERR_NO_MEM, TAG, "allocate host completion semaphore");
    atomic_store(&s_host.stopping, false);
    const usb_host_config_t host = {
        .root_port_unpowered = true,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_host.done);
        s_host.done = NULL;
        return err;
    }
    s_role = USB_HOST;
    if (xTaskCreate(host_worker, "candis_usb_host", 4096, s_host.done, 10, &s_host.task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    err = usb_host_lib_set_root_port_power(true);
    s_host.ready = err == ESP_OK;
    return err;
}

static esp_err_t release_host(void)
{
    if (s_host.task) {
        if (!atomic_load(&s_host.stopping)) {
            usb_host_lib_info_t info;
            ESP_RETURN_ON_ERROR(usb_host_lib_info(&info), TAG, "read host state");
            ESP_RETURN_ON_FALSE(!info.num_clients, ESP_ERR_INVALID_STATE, TAG, "release USB clients first");
            s_host.ready = false;
            const esp_err_t power = usb_host_lib_set_root_port_power(false);
            if (power != ESP_OK && power != ESP_ERR_INVALID_STATE) {
                return power;
            }
            const esp_err_t devices = usb_host_device_free_all();
            if (devices != ESP_OK && devices != ESP_ERR_NOT_FINISHED) {
                return devices;
            }
            atomic_store(&s_host.stopping, true);
        }
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_host.done, pdMS_TO_TICKS(1500) + 1) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "wait for host cleanup");
        s_host.task = NULL;
    } else {
        ESP_RETURN_ON_ERROR(usb_host_uninstall(), TAG, "release unstarted host");
    }
    vSemaphoreDelete(s_host.done);
    s_host.done = NULL;
    s_role = USB_IDLE;
    return ESP_OK;
}
#endif

esp_err_t bsp_usb_host_start(bsp_usb_host_power_mode_t mode, bool limit_500mA)
{
#if CONFIG_CANDIS_USB_HOST
    if (mode != BSP_USB_HOST_POWER_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    if (limit_500mA) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_role == USB_IDLE) {
        err = start_host();
        if (err != ESP_OK && s_role == USB_HOST) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(release_host());
        }
    } else if (s_role == USB_HOST && s_host.ready) {
        err = ESP_OK;
    }
    candis_control_leave();
    return err;
#else
    (void)mode;
    (void)limit_500mA;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t bsp_usb_host_stop(void)
{
#if CONFIG_CANDIS_USB_HOST
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = s_role == USB_HOST ? release_host() : ESP_OK;
    candis_control_leave();
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
