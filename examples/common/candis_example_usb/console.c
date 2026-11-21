/* SPDX-License-Identifier: Apache-2.0 */
#include <unistd.h>
#include "candis_example_usb.h"
#include "sdkconfig.h"

#if CONFIG_CANDIS_EXAMPLE_USB_CDC
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "esp_check.h"
#include "esp_stdio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"
#include "tusb.h"

#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
#include <stdlib.h>
#include "candis_usb_download.h"
#include "esp_rom_sys.h"
#include "hal/usb_utmi_ll.h"
#include "soc/lp_system_reg.h"
#include "soc/soc.h"
#endif

static const char *TAG = "candis_usb";
static const tinyusb_cdcacm_itf_t PORT = TINYUSB_CDC_ACM_0;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_log_lock;
static TaskHandle_t s_worker;
static esp_stdio_handle_t s_sink;
static RingbufHandle_t s_output;
static int s_boot_input = -1;
static candis_example_usb_mode_t s_mode;
static bool s_driver;
static bool s_cdc;
static bool s_running;
static portMUX_TYPE s_line_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_console_tx_after;

#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
static candis_usb_download_sequence_t s_sequence;
static bool s_handshake_enabled;
static bool s_download_requested;
static bool s_download_active;
static bool s_operation;
static bool s_interruptible;

static void on_device_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id == TINYUSB_EVENT_ATTACHED || event->id == TINYUSB_EVENT_DETACHED) {
        portENTER_CRITICAL(&s_line_lock);
        s_sequence = (candis_usb_download_sequence_t){0};
        portEXIT_CRITICAL(&s_line_lock);
    }
}

#endif

static void on_line_state(int interface, cdcacm_event_t *event)
{
    if (interface != PORT || event->type != CDC_EVENT_LINE_STATE_CHANGED) {
        return;
    }
    bool notify = event->line_state_changed_data.dtr;
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_line_lock);
    /* Give terminal clients time to finish opening and clearing their RX buffers. */
    s_console_tx_after = notify ? now + 75000 : 0;
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    const bool requested = s_handshake_enabled && (!s_operation || s_interruptible) && !s_download_active &&
        candis_usb_download_sequence(&s_sequence, event->line_state_changed_data.dtr,
                                     event->line_state_changed_data.rts, now);
    if (requested) {
        s_download_requested = true;
        notify = true;
    }
#endif
    portEXIT_CRITICAL(&s_line_lock);
    if (notify) {
        xTaskNotifyGive(s_worker);
    }
}

#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
static bool claim_download(void)
{
    portENTER_CRITICAL(&s_line_lock);
    const bool requested = s_handshake_enabled && !s_operation && s_download_requested;
    if (!s_operation) {
        s_download_requested = false;
    }
    s_download_active = requested;
    portEXIT_CRITICAL(&s_line_lock);
    return requested;
}
#endif

static int console_open(void *ctx, const char *path, int flags, int mode)
{
    (void)ctx;
    (void)path;
    (void)flags;
    (void)mode;
    return 0;
}

static int console_close(void *ctx, int fd)
{
    (void)ctx;
    (void)fd;
    return 0;
}

static ssize_t console_write(void *ctx, int fd, const void *data, size_t length)
{
    (void)ctx;
    (void)fd;
    /* Preserve the first logs until the host opens CDC, and avoid calling
     * the USB stack under the stdio lock. JTAG still receives every write. */
    xSemaphoreTake(s_log_lock, portMAX_DELAY);
    const bool queued = s_output && length && xRingbufferSend(s_output, data, length, 0) == pdTRUE;
    xSemaphoreGive(s_log_lock);
    if (queued) {
        xTaskNotifyGive(s_worker);
    }
    return length;
}

static ssize_t console_read(void *ctx, int fd, void *data, size_t capacity)
{
    (void)ctx;
    (void)fd;
    if (xSemaphoreTake(s_lock, 0) == pdTRUE) {
        const bool native = s_running && s_mode == CANDIS_EXAMPLE_USB_CONSOLE && tud_cdc_n_connected(PORT);
        size_t count = 0;
        if (native) {
            (void)tinyusb_cdcacm_read(PORT, data, capacity, &count);
        } else if (s_boot_input >= 0) {
            const ssize_t received = read(s_boot_input, data, capacity);
            xSemaphoreGive(s_lock);
            return received;
        }
        xSemaphoreGive(s_lock);
        if (count) {
            return count;
        }
    }
    errno = EAGAIN;
    return -1;
}

static int console_fstat(void *ctx, int fd, struct stat *st)
{
    (void)ctx;
    (void)fd;
    *st = (struct stat){.st_mode = S_IFCHR};
    return 0;
}

static const esp_vfs_fs_ops_t s_console_ops = {
    .open_p = console_open, .close_p = console_close, .write_p = console_write,
    .read_p = console_read, .fstat_p = console_fstat,
};

static bool send_pending_output(void)
{
    if (!s_output || !tud_cdc_n_connected(PORT)) {
        return false;
    }
    portENTER_CRITICAL(&s_line_lock);
    const int64_t ready = s_console_tx_after;
    portEXIT_CRITICAL(&s_line_lock);
    if (esp_timer_get_time() < ready) {
        return true;
    }
    const size_t room = tud_cdc_n_write_available(PORT);
    if (!room) {
        return true;
    }
    size_t count;
    void *data = xRingbufferReceiveUpTo(s_output, &count, 0, room);
    if (!data) {
        return false;
    }
    tinyusb_cdcacm_write_queue(PORT, data, count);
    vRingbufferReturnItem(s_output, data);
    tud_cdc_n_write_flush(PORT);
    return true;
}

static esp_err_t stop_locked(void)
{
    s_running = false;
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    portENTER_CRITICAL(&s_line_lock);
    s_handshake_enabled = false;
    s_download_requested = false;
    s_sequence = (candis_usb_download_sequence_t){0};
    portEXIT_CRITICAL(&s_line_lock);
#endif
    if (s_driver && s_cdc && tud_cdc_n_connected(PORT)) {
        (void)send_pending_output();
        (void)tinyusb_cdcacm_write_flush(PORT, pdMS_TO_TICKS(20));
    }
    if (s_driver) {
        ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "release USB OTG device");
        s_driver = false;
    }
    if (s_cdc) {
        ESP_RETURN_ON_ERROR(tinyusb_cdcacm_deinit(PORT), TAG, "release CDC");
        s_cdc = false;
    }
    xSemaphoreTake(s_log_lock, portMAX_DELAY);
    RingbufHandle_t output = s_output;
    s_output = NULL;
    xSemaphoreGive(s_log_lock);
    if (output) {
        vRingbufferDelete(output);
    }
    return ESP_OK;
}

static void service_task(void *arg)
{
    (void)arg;
    TickType_t wait = portMAX_DELAY;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, wait);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool retry = false;
        if (s_running) {
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
            if (claim_download()) {
                ESP_LOGI(TAG, "USB CDC switching to the ROM downloader");
                /* Let the host finish its final SET_CONTROL_LINE_STATE. */
                vTaskDelay(pdMS_TO_TICKS(50));
                if (stop_locked() == ESP_OK) {
                    usb_utmi_ll_reset_register();
                    usb_utmi_ll_enable_bus_clock(false);
                    vTaskDelay(pdMS_TO_TICKS(20));
                    REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
                    /* Keep the LP download flag through the digital reset. */
                    esp_rom_software_reset_system();
                    abort();
                }
                portENTER_CRITICAL(&s_line_lock);
                s_download_active = false;
                portEXIT_CRITICAL(&s_line_lock);
            }
#endif
            if (s_running && s_mode == CANDIS_EXAMPLE_USB_ECHO) {
                uint8_t buffer[64];
                const size_t space = tud_cdc_n_write_available(PORT);
                const size_t capacity = space < sizeof(buffer) ? space : sizeof(buffer);
                size_t count;
                if (capacity && tinyusb_cdcacm_read(PORT, buffer, capacity, &count) == ESP_OK && count) {
                    tinyusb_cdcacm_write_queue(PORT, buffer, count);
                }
                tud_cdc_n_write_flush(PORT);
                retry = tud_cdc_n_connected(PORT);
            } else if (s_running) {
                retry = send_pending_output();
            }
        }
        wait = retry ? pdMS_TO_TICKS(5) : portMAX_DELAY;
        xSemaphoreGive(s_lock);
    }
}

esp_err_t candis_example_usb_start(candis_example_usb_mode_t mode, bool allow_download)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "allocate USB mutex");
    }
    if (!s_log_lock) {
        s_log_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_log_lock, ESP_ERR_NO_MEM, TAG, "allocate log mutex");
    }
    if (!s_worker) {
        ESP_RETURN_ON_FALSE(xTaskCreate(service_task, "candis_usb", 4096, NULL, 5, &s_worker) == pdPASS,
                            ESP_ERR_NO_MEM, TAG, "create USB service task");
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_running || s_driver) {
        xSemaphoreGive(s_lock);
        return err;
    }
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    const tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG(on_device_event);
#else
    (void)allow_download;
    const tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
#endif
    const tinyusb_config_cdcacm_t cdc = {.cdc_port = PORT, .callback_line_state_changed = on_line_state};
    err = tinyusb_driver_install(&usb);
    if (err == ESP_OK) {
        s_driver = true;
        err = tinyusb_cdcacm_init(&cdc);
    }
    if (err == ESP_OK) {
        s_cdc = true;
        if (mode == CANDIS_EXAMPLE_USB_CONSOLE) {
            RingbufHandle_t output = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);
            if (!output) {
                err = ESP_ERR_NO_MEM;
            } else {
                xSemaphoreTake(s_log_lock, portMAX_DELAY);
                s_output = output;
                xSemaphoreGive(s_log_lock);
            }
            if (err == ESP_OK && !s_sink) {
                const esp_stdio_driver_config_t sink = {.vfs_ops = &s_console_ops, .path = "/"};
                err = esp_stdio_register_driver(&sink, &s_sink);
                if (err == ESP_OK) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
                    s_boot_input = open("/dev/usbserjtag", O_RDONLY | O_NONBLOCK);
#endif
                    /* An unplugged boot console can report a short write. CDC
                     * owns the write result. Keep the mux entry registered across
                     * detach so concurrent printf calls can still use JTAG. */
                    err = esp_stdio_set_primary_driver(s_sink, NULL);
                    if (err != ESP_OK) {
                        esp_stdio_unregister_driver(s_sink);
                        s_sink = NULL;
                        if (s_boot_input >= 0) {
                            close(s_boot_input);
                            s_boot_input = -1;
                        }
                    }
                }
            }
        }
    }
    if (err == ESP_OK) {
        s_mode = mode;
        s_running = true;
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
        portENTER_CRITICAL(&s_line_lock);
        s_handshake_enabled = allow_download;
        portEXIT_CRITICAL(&s_line_lock);
#endif
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(stop_locked());
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        if (mode == CANDIS_EXAMPLE_USB_ECHO) {
            xTaskNotifyGive(s_worker);
        }
        ESP_LOGI(TAG, "USB OTG CDC %s ready", mode == CANDIS_EXAMPLE_USB_CONSOLE ? "console" : "echo");
    }
    return err;
}

esp_err_t candis_example_usb_stop(void)
{
    if (!s_lock || !s_log_lock) {
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const esp_err_t err = stop_locked();
    xSemaphoreGive(s_lock);
    return err;
}

#else
esp_err_t candis_example_usb_start(candis_example_usb_mode_t mode, bool allow_download)
{
    (void)mode;
    (void)allow_download;
    return ESP_OK;
}

esp_err_t candis_example_usb_stop(void)
{
    return ESP_OK;
}

#endif

bool candis_example_usb_read(char *byte)
{
    return read(STDIN_FILENO, byte, 1) == 1;
}

static bool begin_operation(bool interruptible)
{
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    portENTER_CRITICAL(&s_line_lock);
    const bool allowed = !s_download_active && !s_operation;
    if (allowed) {
        s_operation = true;
        s_interruptible = interruptible;
        s_download_requested = false;
        s_sequence = (candis_usb_download_sequence_t){0};
    }
    portEXIT_CRITICAL(&s_line_lock);
    return allowed;
#else
    (void)interruptible;
    return true;
#endif
}

bool candis_example_usb_begin_operation(void)
{
    return begin_operation(false);
}

bool candis_example_usb_begin_interruptible_operation(void)
{
    return begin_operation(true);
}

bool candis_example_usb_download_pending(void)
{
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    portENTER_CRITICAL(&s_line_lock);
    const bool pending = s_interruptible && s_download_requested;
    portEXIT_CRITICAL(&s_line_lock);
    return pending;
#else
    return false;
#endif
}

void candis_example_usb_end_operation(void)
{
#if CONFIG_CANDIS_EXAMPLE_USB_DOWNLOAD
    portENTER_CRITICAL(&s_line_lock);
    s_operation = false;
    s_interruptible = false;
    const bool requested = s_download_requested;
    portEXIT_CRITICAL(&s_line_lock);
    if (requested) {
        xTaskNotifyGive(s_worker);
    }
#endif
}
