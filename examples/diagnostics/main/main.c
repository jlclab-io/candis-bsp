/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include "bsp/candis_rtc.h"
#include "bsp/candis_storage.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifdef CONFIG_CANDIS_DIAGNOSTICS_STORAGE
#include <dirent.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "candis_diagnostics";

#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG
esp_err_t candis_diagnostics_start_otg(void);
#endif
#ifdef CONFIG_CANDIS_DIAGNOSTICS_AUDIO
esp_err_t candis_diagnostics_audio(void);
#endif

#ifdef CONFIG_CANDIS_DIAGNOSTICS_STORAGE
static const char *MOUNT_PATH = "/sdcard";

static void inspect_card(void)
{
    sdmmc_card_t *card;
#ifdef CONFIG_CANDIS_DIAGNOSTICS_FIXTURE
    const candis_storage_profile_t profile = CANDIS_STORAGE_FIXTURE;
#else
    const candis_storage_profile_t profile = CANDIS_STORAGE_EXPANSION;
#endif
#ifdef CONFIG_CANDIS_DIAGNOSTICS_DC1SW
    const bool manage_power = true;
#else
    const bool manage_power = false;
#endif
    for (unsigned cycle = 1; cycle <= 3; ++cycle) {
        esp_err_t err = candis_storage_mount(MOUNT_PATH, profile, manage_power, &card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Card mount %u: %s", cycle, esp_err_to_name(err));
            return;
        }
        uint64_t capacity = (uint64_t)card->csd.capacity * card->csd.sector_size;
        ESP_LOGI(TAG, "Card mount %u: %" PRIu64 " MiB, %d kHz, %u-bit", cycle, capacity >> 20,
                 card->real_freq_khz, 1U << card->log_bus_width);
        DIR *root = opendir(MOUNT_PATH);
        if (root) {
            unsigned entries = 0;
            errno = 0;
            while (entries < 32 && readdir(root)) {
                ++entries;
            }
            ESP_LOGI(TAG, "Card root sampled: entries=%u errno=%d", entries, errno);
            closedir(root);
        } else {
            ESP_LOGE(TAG, "Open card root: errno=%d", errno);
        }
        err = candis_storage_unmount();
        ESP_LOGI(TAG, "Card unmount %u: %s", cycle, esp_err_to_name(err));
        if (err != ESP_OK) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
#endif

void app_main(void)
{
    struct tm utc;
    esp_err_t err = candis_rtc_get_time(&utc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC: %04d-%02d-%02d %02d:%02d:%02d (calendar)", utc.tm_year + 1900,
                 utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    } else {
        ESP_LOGW(TAG, "RTC: %s", esp_err_to_name(err));
    }
#ifdef CONFIG_CANDIS_DIAGNOSTICS_STORAGE
    inspect_card();
#endif
#ifdef CONFIG_CANDIS_DIAGNOSTICS_AUDIO
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_diagnostics_audio());
#endif
#ifdef CONFIG_CANDIS_DIAGNOSTICS_OTG
    ESP_ERROR_CHECK(candis_diagnostics_start_otg());
#endif
}
