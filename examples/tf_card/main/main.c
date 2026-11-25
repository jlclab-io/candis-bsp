/* SPDX-License-Identifier: Apache-2.0 */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>

#include "bsp/candis.h"
#include "bsp/candis_power.h"
#include "bsp/candis_storage.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "candis_example_usb.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tf_verify.h"

static const char *TAG = "candis_tf";

#ifdef CONFIG_CANDIS_TF_MANAGE_POWER
static const bool MANAGE_POWER = true;
#else
static const bool MANAGE_POWER = false;
#endif

static candis_storage_config_t storage_config(void)
{
    candis_storage_config_t config = CANDIS_STORAGE_CONFIG_DEFAULT();
    config.slot.clk = GPIO_NUM_24;
    config.slot.cmd = GPIO_NUM_25;
    config.slot.d0 = GPIO_NUM_20;
    config.slot.d1 = GPIO_NUM_21;
    config.slot.d2 = GPIO_NUM_22;
    config.slot.d3 = GPIO_NUM_23;
#ifdef CONFIG_CANDIS_TF_EXPANSION
    config.slot.cd = GPIO_NUM_39;
#else
    config.slot.cd = GPIO_NUM_35;
#endif
    config.slot.wp = SDMMC_SLOT_NO_WP;
    config.slot.width = 4;
    config.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    config.card_detect_active_low = true;
    config.use_dc1sw = true;
    return config;
}

/* On IDF 6.1 the browser itself only issues directory reads. */
static const bool BROWSE_READ_ONLY = ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 2, 0);
enum { DISPLAY_ROWS = 10, LOG_ROWS = 16, ENTRY_LABEL_SIZE = 64 };

static lv_obj_t *s_status_label;
static lv_obj_t *s_info_label;
static lv_obj_t *s_entry_labels[DISPLAY_ROWS];
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
static lv_obj_t *s_write_button;
static atomic_bool s_write_requested;
#endif
static bool s_display_ready;
static sdmmc_card_t *s_mounted_card;
static bool s_unmount_pending;

static void display_update(const char *status, const char *info,
                           const char entries[][ENTRY_LABEL_SIZE], unsigned entry_count)
{
    if (!s_display_ready || !bsp_display_lock(100)) {
        return;
    }
    lv_label_set_text(s_status_label, status);
    lv_label_set_text(s_info_label, info);
    for (unsigned i = 0; i < DISPLAY_ROWS; ++i) {
        lv_label_set_text(s_entry_labels[i], i < entry_count ? entries[i] : "");
    }
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
    lv_obj_set_hidden(s_write_button, !info[0]);
#endif
    bsp_display_unlock();
}

static void display_set_status(const char *status)
{
    display_update(status, "", NULL, 0);
}

#ifdef CONFIG_CANDIS_TF_WRITE_TEST
static void write_test_clicked(lv_event_t *event)
{
    (void)event;
    atomic_store(&s_write_requested, true);
}
#endif

static esp_err_t create_display_ui(lv_display_t *display)
{
    if (!bsp_display_lock(1000)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101817), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_pos(title, 24, 18);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xe8f2ec), 0);
    lv_label_set_text(title, "TF Card / File System");

    s_status_label = lv_label_create(screen);
    lv_obj_set_pos(s_status_label, 24, 55);
    lv_obj_set_width(s_status_label, 430);
    lv_obj_set_style_text_font(s_status_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xa8eeb9), 0);

    s_info_label = lv_label_create(screen);
    lv_obj_set_pos(s_info_label, 24, 86);
    lv_obj_set_width(s_info_label, 430);
    lv_obj_set_style_text_font(s_info_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(s_info_label, lv_color_hex(0x9fb3aa), 0);

    for (unsigned i = 0; i < DISPLAY_ROWS; ++i) {
        s_entry_labels[i] = lv_label_create(screen);
        lv_obj_set_pos(s_entry_labels[i], 30, 118 + (int)i * 25);
        lv_label_set_long_mode(s_entry_labels[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(s_entry_labels[i], 420, 22);
        lv_obj_set_style_text_font(s_entry_labels[i], LV_FONT_DEFAULT, 0);
        lv_obj_set_style_text_color(s_entry_labels[i], lv_color_hex(0xe0e9e3), 0);
        lv_label_set_text(s_entry_labels[i], "");
    }
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
    s_write_button = lv_button_create(screen);
    lv_obj_set_pos(s_write_button, 24, 391);
    lv_obj_set_size(s_write_button, 210, 52);
    lv_obj_set_hidden(s_write_button, true);
    lv_obj_set_style_bg_color(s_write_button, lv_color_hex(0x24624c), 0);
    lv_obj_add_event_cb(s_write_button, write_test_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *write_label = lv_label_create(s_write_button);
    lv_label_set_text(write_label, "4 KiB write / read test");
    lv_obj_center(write_label);
#endif
    s_display_ready = true;
    bsp_display_unlock();
    display_set_status("TF card: waiting for hot-plug");
    return ESP_OK;
}

static esp_err_t list_card(const sdmmc_card_t *card, const char *status)
{
    DIR *root = opendir(CANDIS_EXAMPLE_TF_PATH);
    if (!root) {
        ESP_LOGE(TAG, "opendir errno=%d", errno);
        return ESP_FAIL;
    }
    char entry_names[DISPLAY_ROWS][ENTRY_LABEL_SIZE];
    unsigned entry_count = 0;
    esp_err_t result = ESP_OK;
    while (entry_count < LOG_ROWS) {
        errno = 0;
        const struct dirent *entry = readdir(root);
        if (!entry) {
            result = errno == 0 ? ESP_OK : ESP_FAIL;
            break;
        }
        ESP_LOGI(TAG, "TF_ENTRY %s", entry->d_name);
        if (entry_count < DISPLAY_ROWS) {
            snprintf(entry_names[entry_count], sizeof(entry_names[entry_count]), "%u  %.58s", entry_count + 1,
                     entry->d_name);
        }
        ++entry_count;
    }
    if (closedir(root) != 0) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        return result;
    }
    const uint64_t mib = (uint64_t)card->csd.capacity * card->csd.sector_size / (1024 * 1024);
    ESP_LOGI(TAG, "TF_LIST capacity=%" PRIu64 "MiB bus=%u-bit clock=%dkHz entries_shown=%u",
             mib, 1U << card->log_bus_width, card->real_freq_khz, entry_count);
    char info[96];
    snprintf(info, sizeof(info), "%s  %" PRIu64 " MiB  %u-bit  %d kHz",
             BROWSE_READ_ONLY ? "Read-only" : "Browse",
             mib, 1U << card->log_bus_width, card->real_freq_khz);
    display_update(status, info, entry_names, entry_count > DISPLAY_ROWS ? DISPLAY_ROWS : entry_count);
    return ESP_OK;
}

static esp_err_t unmount_card(void)
{
    if (!s_mounted_card && !s_unmount_pending) {
        return ESP_OK;
    }
    /* Unmount can consume the handle even if further BSP cleanup fails. */
    s_mounted_card = NULL;
    const esp_err_t err = candis_storage_unmount();
    s_unmount_pending = err != ESP_OK;
    return err;
}

static esp_err_t mount_card(bool read_only)
{
    if (s_mounted_card || s_unmount_pending) {
        return ESP_ERR_INVALID_STATE;
    }
    const candis_storage_mount_config_t config = {.read_only = read_only};
    const esp_err_t err = candis_storage_mount_with_config(CANDIS_EXAMPLE_TF_PATH, &config,
                                                           &s_mounted_card);
    if (err != ESP_OK) {
        /* A failed mount can leave BSP cleanup pending. */
        s_unmount_pending = candis_storage_unmount() != ESP_OK;
    }
    return err;
}

static esp_err_t show_directory(const char *status)
{
    esp_err_t err = s_mounted_card ? ESP_OK : mount_card(BROWSE_READ_ONLY);
    if (err != ESP_OK) {
        char message[80];
        snprintf(message, sizeof(message), "TF mount failed: %s", esp_err_to_name(err));
        display_set_status(message);
        return err;
    }
    err = list_card(s_mounted_card, status);
    if (err != ESP_OK) {
        (void)unmount_card();
        display_set_status("TF card: directory read failed");
    }
    return err;
}

#ifdef CONFIG_CANDIS_TF_WRITE_TEST
static void run_write_test(const char *source, bool *card_present)
{
    display_set_status("TF card: testing 4 KiB write / read");
    esp_err_t result = unmount_card();
    if (result != ESP_OK) {
        display_set_status("TF write test: browser unmount failed");
        ESP_LOGW(TAG, "TF write test could not release browser mount: %s", esp_err_to_name(result));
        return;
    }

    const esp_err_t mount = mount_card(false);
    const esp_err_t verify = mount == ESP_OK ? candis_example_tf_verify() : mount;
    const esp_err_t unmount = unmount_card();
    result = verify == ESP_OK ? unmount : verify;

    const char *status = result == ESP_OK ? "TF write / read / delete PASS" : "TF write / read / delete FAILED";
    bool present = false;
    const esp_err_t detect = candis_storage_detect_read(&present);
    esp_err_t browse = detect;
    if (s_unmount_pending) {
        browse = unmount;
        display_set_status("TF card: retrying mount release");
    } else if (detect != ESP_OK) {
        display_set_status("TF card: detect unavailable");
    } else if (!present) {
        browse = ESP_ERR_NOT_FOUND;
        *card_present = false;
        ESP_LOGI(TAG, "TF_CARD removed");
        display_set_status("TF card: removed");
    } else {
        browse = show_directory(status);
        if (browse != ESP_OK) {
            display_set_status(result == ESP_OK ? "TF test PASS, browse mount failed" :
                               "TF test FAILED, browse mount failed");
        }
    }
    ESP_LOGI(TAG, "TF_RESULT operation=write_verify source=%s mount=%s verify=%s unmount=%s browse=%s",
             source, esp_err_to_name(mount), esp_err_to_name(verify), esp_err_to_name(unmount),
             esp_err_to_name(browse));
}
#endif

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
    const candis_storage_config_t storage = storage_config();
    ESP_ERROR_CHECK(candis_storage_configure(&storage));
    candis_display_config_t display_config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    display_config.manage_panel_power = true;
    lv_display_t *display = NULL;
    esp_err_t display_err = candis_display_start(&display_config, &display);
    if (display_err == ESP_OK) {
        display_err = create_display_ui(display);
    }
    if (display_err == ESP_OK) {
        ESP_LOGI(TAG, "TF_DISPLAY_READY path=%s hotplug=live read_only=%d entries=%d",
                 CANDIS_EXAMPLE_TF_PATH, BROWSE_READ_ONLY, DISPLAY_ROWS);
    } else {
        ESP_LOGW(TAG, "AMOLED filesystem UI unavailable: %s", esp_err_to_name(display_err));
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
    ESP_LOGI(TAG, "TF_READY commands=L:list,W:write_verify,Q:release");
#else
    ESP_LOGI(TAG, "TF_READY commands=L:list,Q:release");
#endif
    if (MANAGE_POWER && !candis_example_usb_begin_operation()) {
        return;
    }
    esp_err_t start = candis_storage_detect_start(MANAGE_POWER);
    if (MANAGE_POWER && start == ESP_OK) {
        ESP_LOGI(TAG, "Send Q to release DC1SW before downloading another example");
    }
    if (start != ESP_OK) {
        ESP_LOGE(TAG, "TF_DETECT start=%s (check DC1SW and the board profile)", esp_err_to_name(start));
        char status[80];
        snprintf(status, sizeof(status), "TF detect failed %s", esp_err_to_name(start));
        display_set_status(status);
    }

    bool present = false;
    bool detection_reported = false;
    unsigned changes = 0;
    esp_err_t result = start;
    while (result == ESP_OK) {
        char command = 0;
        if (candis_example_usb_read(&command) && (command == 'Q' || command == 'q')) {
            break;
        }
        bool refresh = s_unmount_pending && unmount_card() == ESP_OK;
        bool detected;
        const esp_err_t detect = candis_storage_detect_read(&detected);
        if (detect == ESP_ERR_INVALID_STATE) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }
        if (detect != ESP_OK) {
            result = detect;
            break;
        }
        if (!detection_reported && !detected) {
            detection_reported = true;
            ESP_LOGI(TAG, "TF_CARD absent");
            display_set_status("TF card: no card");
        }
        if (detected == present) {
            changes = 0;
        } else if (++changes == 3) {
            changes = 0;
            present = detected;
            detection_reported = true;
            ESP_LOGI(TAG, "TF_CARD %s", present ? "inserted" : "removed");
            refresh = present;
            if (!present) {
                display_set_status("TF card: removed");
                ESP_ERROR_CHECK_WITHOUT_ABORT(unmount_card());
            }
        }
        refresh = refresh || command == 'L' || command == 'l';
        const char *write_source = NULL;
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
        const bool touched = atomic_exchange(&s_write_requested, false);
        if (command == 'W' || command == 'w') {
            write_source = "console";
        } else if (touched) {
            write_source = "touch";
        }
#endif
        if (present && detected && !s_unmount_pending && (refresh || write_source) &&
            (MANAGE_POWER || candis_example_usb_begin_operation())) {
#ifdef CONFIG_CANDIS_TF_WRITE_TEST
            if (write_source) {
                run_write_test(write_source, &present);
            }
#endif
            if (!write_source) {
                const esp_err_t err = show_directory("TF card: mounted");
                ESP_LOGI(TAG, "TF_RESULT operation=list result=%s", esp_err_to_name(err));
            }
            if (!MANAGE_POWER) {
                candis_example_usb_end_operation();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (start == ESP_OK) {
        display_set_status(result == ESP_OK ? "TF card: released" : "TF card: detect failed");
    }
    for (;;) {
        const esp_err_t unmount = unmount_card();
        const esp_err_t stop = unmount == ESP_OK ? candis_storage_detect_stop() : ESP_ERR_INVALID_STATE;
        ESP_LOGI(TAG, "TF_RELEASE read=%s unmount=%s detect=%s",
                 esp_err_to_name(result), esp_err_to_name(unmount), esp_err_to_name(stop));
        if (stop == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    candis_storage_reset_config();
    if (MANAGE_POWER) {
        candis_example_usb_end_operation();
    }
}
