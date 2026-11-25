/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include "bsp/candis_baseboard_bus.h"
#include "bsp/candis_dock.h"
#include "bsp/candis_power.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tf_verify.h"
#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
#include "bsp/esp-bsp.h"
#endif

static const char *TAG = "candis_expansion";
enum { EXP_I2C = I2C_NUM_1, EEPROM_ADDR = 0x50 };
static candis_expansion_power_handle_t s_expansion_power;

static esp_err_t uart_released(void)
{
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    if (CONFIG_ESP_CONSOLE_UART_TX_GPIO == GPIO_NUM_58 ||
        CONFIG_ESP_CONSOLE_UART_TX_GPIO == GPIO_NUM_59 ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == GPIO_NUM_58 ||
        CONFIG_ESP_CONSOLE_UART_RX_GPIO == GPIO_NUM_59) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return uart_is_driver_installed(UART_NUM_0) ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static esp_err_t ensure_expansion_power(void)
{
    candis_power_state_t power;
    esp_err_t err = candis_power_read(&power);
    if (err != ESP_OK || power.dc1sw) {
        return err;
    }
    if (!s_expansion_power) {
        err = candis_expansion_power_claim(true, &s_expansion_power);
        if (err != ESP_OK) {
            const esp_err_t cleanup = candis_expansion_power_unclaim(&s_expansion_power);
            ESP_LOGW(TAG, "power claim=%s cleanup=%s", esp_err_to_name(err), esp_err_to_name(cleanup));
            return err;
        }
    }
    err = candis_power_read(&power);
    return err == ESP_OK && power.dc1sw ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t probe_testboard(candis_baseboard_info_t *identity)
{
    ESP_RETURN_ON_ERROR(ensure_expansion_power(), TAG, "enable expansion power");
    ESP_RETURN_ON_ERROR(uart_released(), TAG, "release UART0");
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = EXP_I2C,
        .scl_io_num = GPIO_NUM_58,
        .sda_io_num = GPIO_NUM_59,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "open expansion I2C");
    const esp_err_t probe = candis_baseboard_probe_on_bus(bus, EEPROM_ADDR, identity);
    const esp_err_t close = i2c_del_master_bus(bus);
    return probe == ESP_OK ? close : probe;
}

#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
static lv_obj_t *s_status;

static esp_err_t start_display(const char *name)
{
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    config.brightness_percent = 100;
    config.enable_touch = false;
    config.async_flush = true;
    config.manage_panel_power = true;
    lv_display_t *display;
    esp_err_t err = candis_display_start(&config, &display);
    if (err != ESP_OK) {
        return err;
    }
    err = bsp_display_brightness_set(config.brightness_percent);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        return err;
    }
    if (!bsp_display_lock(250)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_color(title, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_outline_stroke_color(title, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_outline_stroke_width(title, 1, 0);
    lv_obj_set_style_text_outline_stroke_opa(title, LV_OPA_COVER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 52);
    s_status = lv_label_create(screen);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_outline_stroke_color(s_status, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_outline_stroke_width(s_status, 1, 0);
    lv_obj_set_style_text_outline_stroke_opa(s_status, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_status);
    bsp_display_unlock();
    ESP_LOGI(TAG, "Display ready at %u%% brightness", config.brightness_percent);
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
    esp_err_t err = probe_testboard(&identity);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Baseboard %s, hardware=0x%04x", identity.name, identity.hardware_revision);
        err = candis_dock_open(false, &identity);
    }
    if (err == ESP_OK) {
#ifdef CONFIG_CANDIS_EXPANSION_DISPLAY
        err = start_display(identity.name);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Display unavailable: %s", esp_err_to_name(err));
        }
#endif
        err = monitor_dock(&identity);
        const esp_err_t close = candis_dock_close();
        err = err == ESP_OK ? close : err;
    }
    const esp_err_t power = candis_expansion_power_unclaim(&s_expansion_power);
    err = err == ESP_OK ? power : err;
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
