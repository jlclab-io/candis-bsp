/* SPDX-License-Identifier: Apache-2.0 */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include "bsp/candis_power.h"
#include "bsp/candis_storage.h"
#include "candis_example_usb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tf_verify.h"

static const char *TAG = "candis_tf";

#ifdef CONFIG_CANDIS_TF_EXPANSION
static const candis_storage_profile_t PROFILE = CANDIS_STORAGE_EXPANSION;
#else
static const candis_storage_profile_t PROFILE = CANDIS_STORAGE_FIXTURE;
#endif

#ifdef CONFIG_CANDIS_TF_MANAGE_POWER
static const bool MANAGE_POWER = true;
#else
static const bool MANAGE_POWER = false;
#endif

static esp_err_t list_card(const sdmmc_card_t *card)
{
    DIR *root = opendir(CANDIS_EXAMPLE_TF_PATH);
    if (!root) {
        ESP_LOGE(TAG, "opendir errno=%d", errno);
        return ESP_FAIL;
    }
    unsigned entries = 0;
    esp_err_t result = ESP_OK;
    while (entries < 16) {
        errno = 0;
        const struct dirent *entry = readdir(root);
        if (!entry) {
            result = errno == 0 ? ESP_OK : ESP_FAIL;
            break;
        }
        ESP_LOGI(TAG, "TF_ENTRY %s", entry->d_name);
        ++entries;
    }
    if (closedir(root) != 0) {
        result = ESP_FAIL;
    }
    const uint64_t mib = (uint64_t)card->csd.capacity * card->csd.sector_size / (1024 * 1024);
    ESP_LOGI(TAG, "TF_LIST capacity=%" PRIu64 "MiB bus=%u-bit clock=%dkHz entries_shown=%u",
             mib, 1U << card->log_bus_width, card->real_freq_khz, entries);
    return result;
}

static void use_card(bool write_test)
{
    if (!MANAGE_POWER && !candis_example_usb_begin_operation()) {
        return;
    }
    const candis_storage_mount_config_t config = {.read_only = !write_test};
    sdmmc_card_t *card = NULL;
    const esp_err_t mount = candis_storage_mount_with_config(CANDIS_EXAMPLE_TF_PATH, PROFILE, &config, &card);
    esp_err_t result = mount;
    if (mount == ESP_OK) {
        result = write_test ? candis_example_tf_verify() : list_card(card);
    }
    const esp_err_t unmount = candis_storage_unmount();
    if (!MANAGE_POWER) {
        candis_example_usb_end_operation();
    }
    ESP_LOGI(TAG, "TF_RESULT operation=%s mount=%s result=%s unmount=%s",
             write_test ? "write_verify" : "list", esp_err_to_name(mount),
             esp_err_to_name(result), esp_err_to_name(unmount));
}

void app_main(void)
{
    ESP_ERROR_CHECK(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    vTaskDelay(pdMS_TO_TICKS(1500));
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
    ESP_LOGI(TAG, "TF_READY commands=L:list,W:write_verify,Q:release");
#else
    ESP_LOGI(TAG, "TF_READY commands=L:list,Q:release");
#endif
    if (MANAGE_POWER && !candis_example_usb_begin_operation()) {
        return;
    }
    esp_err_t start = ESP_OK;
    if (MANAGE_POWER) {
        start = candis_expansion_power_acquire(true);
        ESP_LOGI(TAG, "Send Q to release DC1SW before downloading another example");
    }
    if (start == ESP_OK) {
        start = candis_storage_detect_start(PROFILE);
    }
    if (start != ESP_OK) {
        ESP_LOGE(TAG, "TF_DETECT start=%s (check DC1SW and the board profile)", esp_err_to_name(start));
    }

    bool present = false;
    bool detection_reported = false;
    unsigned changes = 0;
    esp_err_t result = start;
    while (result == ESP_OK) {
        bool detected;
        result = candis_storage_detect_read(&detected);
        if (result != ESP_OK) {
            break;
        }
        if (!detection_reported && !detected) {
            detection_reported = true;
            ESP_LOGI(TAG, "TF_CARD absent");
        }
        if (detected == present) {
            changes = 0;
        } else if (++changes == 3) {
            changes = 0;
            present = detected;
            detection_reported = true;
            ESP_LOGI(TAG, "TF_CARD %s", present ? "inserted" : "removed");
            if (present) {
                use_card(false);
            }
        }
        char command;
        if (candis_example_usb_read(&command)) {
            if (command == 'Q' || command == 'q') {
                break;
            }
            if (command == 'L' || command == 'l') {
                use_card(false);
            }
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
            if (command == 'W' || command == 'w') {
                use_card(true);
            }
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    for (;;) {
        const esp_err_t unmount = candis_storage_unmount();
        const esp_err_t stop = candis_storage_detect_stop();
        esp_err_t power = ESP_OK;
        if (MANAGE_POWER && stop == ESP_OK) {
            power = candis_expansion_power_release();
        }
        ESP_LOGI(TAG, "TF_RELEASE read=%s unmount=%s detect=%s power=%s",
                 esp_err_to_name(result), esp_err_to_name(unmount),
                 esp_err_to_name(stop), esp_err_to_name(power));
        if (stop == ESP_OK && power == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (MANAGE_POWER) {
        candis_example_usb_end_operation();
    }
}
