/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include "bsp/candis_dock.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tf_verify.h"
#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
#include "bsp/esp-bsp.h"
#endif

static const char *TAG = "candis_expansion";

#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
static lv_obj_t *s_status;

static esp_err_t start_display(const char *name)
{
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    config.enable_touch = false;
    config.async_flush = true;
    lv_display_t *display;
    esp_err_t err = candis_display_start(&config, &display);
    if (err != ESP_OK) {
        return err;
    }
    if (!bsp_display_lock(250)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *title = lv_label_create(lv_display_get_screen_active(display));
    lv_label_set_text(title, name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);
    s_status = lv_label_create(lv_display_get_screen_active(display));
    lv_obj_center(s_status);
    bsp_display_unlock();
    return ESP_OK;
}

static void show_status(const char *message)
{
    if (s_status && bsp_display_lock(5)) {
        lv_label_set_text(s_status, message);
        bsp_display_unlock();
    }
}
#else
static void show_status(const char *message)
{
    (void)message;
}
#endif

static esp_err_t list_card(const candis_baseboard_info_t *identity)
{
    sdmmc_card_t *card;
    const candis_storage_mount_config_t config = {
#ifdef CONFIG_CANDIS_EXPANSION_TF_WRITE_TEST
        .read_only = false,
#else
        .read_only = true,
#endif
    };
    esp_err_t err = candis_dock_mount_with_config(identity, CANDIS_EXAMPLE_TF_PATH, &config, &card);
    if (err != ESP_OK) {
        return err;
    }
    unsigned entries = 0;
    DIR *directory = opendir(CANDIS_EXAMPLE_TF_PATH);
    if (directory) {
        errno = 0;
        struct dirent *entry;
        while (entries < 16 && (entry = readdir(directory)) != NULL) {
            ESP_LOGI(TAG, "Card entry: %s", entry->d_name);
            ++entries;
            errno = 0;
        }
        err = errno ? ESP_FAIL : ESP_OK;
        if (closedir(directory) != 0) {
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "open card directory: errno=%d", errno);
        err = ESP_FAIL;
    }
#ifdef CONFIG_CANDIS_EXPANSION_TF_WRITE_TEST
    if (err == ESP_OK) {
        show_status("Verifying 4 KiB file");
        err = candis_example_tf_verify();
    }
#endif
    char status[120];
    snprintf(status, sizeof(status), "TF %d kHz / %u-bit\nScanned %u entries\n%s",
             card->real_freq_khz, 1U << card->log_bus_width, entries, esp_err_to_name(err));
    ESP_LOGI(TAG, "%s", status);
    show_status(status);
    return err;
}

static esp_err_t monitor_dock(const candis_baseboard_info_t *identity)
{
    enum { DETECT_POLL_MS = 40, DETECT_STABLE_SAMPLES = 3, MONITOR_SECONDS = 60 };
    esp_err_t err = candis_dock_detect_start(identity);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_storage_detect_stop());
        return err;
    }
#ifdef CONFIG_CANDIS_EXPANSION_DOCK_MODULE_SCAN
    candis_dock_module_scan_t scan;
    const esp_err_t modules = candis_dock_probe_modules(identity, &scan);
    if (modules == ESP_OK) {
        ESP_LOGI(TAG, "Dock expansion I2C 0x51=%s 0x52=%s",
                 scan.address_51 ? "ACK" : "no ACK", scan.address_52 ? "ACK" : "no ACK");
    } else {
        ESP_LOGW(TAG, "Dock expansion I2C: %s", esp_err_to_name(modules));
    }
#endif

    const int64_t deadline = esp_timer_get_time() + MONITOR_SECONDS * INT64_C(1000000);
    unsigned samples = 0;
    bool previous = false;
    bool present = false;
    esp_err_t last_scan = ESP_ERR_NOT_FOUND;
    ESP_LOGI(TAG, "Monitoring Dock TF insert and remove for %d seconds", MONITOR_SECONDS);
    show_status("Insert a FAT-formatted TF card");
    while (esp_timer_get_time() < deadline) {
        bool detected;
        err = candis_storage_detect_read(&detected);
        if (err != ESP_OK) {
            break;
        }
        if (detected != previous) {
            previous = detected;
            samples = 0;
        }
        if (detected != present && ++samples == DETECT_STABLE_SAMPLES) {
            present = detected;
            ESP_LOGI(TAG, "Dock TF %s", present ? "inserted" : "removed");
            if (present) {
                show_status("Reading TF directory");
                last_scan = list_card(identity);
                err = candis_storage_unmount();
                ESP_LOGI(TAG, "Dock TF read=%s unmount=%s", esp_err_to_name(last_scan), esp_err_to_name(err));
                if (last_scan != ESP_OK) {
                    show_status(esp_err_to_name(last_scan));
                }
                if (err != ESP_OK) {
                    break;
                }
            } else {
                show_status("Insert a FAT-formatted TF card");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DETECT_POLL_MS));
    }
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_storage_unmount());
    }
    const esp_err_t release = candis_storage_detect_stop();
    if (err != ESP_OK) {
        return err;
    }
    return release == ESP_OK ? last_scan : release;
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    candis_baseboard_info_t identity;
    esp_err_t err = candis_baseboard_probe(&identity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Baseboard %s, hardware=0x%04x", identity.name, identity.hardware_revision);
#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
        err = start_display(identity.name);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Display unavailable: %s", esp_err_to_name(err));
        }
#endif
        err = monitor_dock(&identity);
    }
    ESP_LOGI(TAG, "Expansion diagnostic: %s", esp_err_to_name(err));
#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
    if (s_status) {
        if (err != ESP_OK) {
            show_status(esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
    }
#endif
}
