/* SPDX-License-Identifier: Apache-2.0 */
#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CAPI "candis"

static candis_storage_config_t test_storage_config(void)
{
    candis_storage_config_t config = CANDIS_STORAGE_CONFIG_DEFAULT();
    config.slot.clk = GPIO_NUM_24;
    config.slot.cmd = GPIO_NUM_25;
    config.slot.d0 = GPIO_NUM_20;
    config.slot.d1 = GPIO_NUM_21;
    config.slot.d2 = GPIO_NUM_22;
    config.slot.d3 = GPIO_NUM_23;
    config.slot.cd = GPIO_NUM_39;
    config.slot.wp = SDMMC_SLOT_NO_WP;
    config.slot.width = 4;
    config.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    config.card_detect_active_low = true;
    config.use_dc1sw = true;
    return config;
}

static void test_storage(void)
{
    candis_storage_config_t config = test_storage_config();
    candis_storage_mount_config_t mount_config = {0};
    api_test_require(CAPI, "candis_storage_configure", API_ASPECT_BOUNDARY,
                     candis_storage_configure(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    config.slot.width = 0;
    api_test_require(CAPI, "candis_storage_configure", API_ASPECT_BOUNDARY,
                     candis_storage_configure(&config), ESP_ERR_INVALID_ARG, "zero bus width");
    config = test_storage_config();
    api_test_require(CAPI, "candis_storage_configure", API_ASPECT_NORMAL,
                     candis_storage_configure(&config), ESP_OK, "copy formal Dock four-bit slot");
    api_test_require(CAPI, "candis_storage_configure", API_ASPECT_LIFECYCLE,
                     candis_storage_configure(&config), ESP_OK, "repeat while idle");

    sdmmc_host_t host = {0};
    sdmmc_host_t sdspi_host = {0};
    sdmmc_slot_config_t slot = {0};
    sdspi_device_config_t sdspi = {0};
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &host);
    api_test_check_bool(CAPI, "bsp_sdcard_get_sdmmc_host", API_ASPECT_NORMAL,
                        host.slot == SDMMC_HOST_SLOT_0 && host.max_freq_khz > 0,
                        "generic SDMMC host populated");
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
    api_test_check_bool(CAPI, "bsp_sdcard_sdmmc_get_slot", API_ASPECT_NORMAL,
                        slot.clk == GPIO_NUM_24 && slot.cmd == GPIO_NUM_25 &&
                        slot.width == 4, "configured slot copied");
    bsp_sdcard_get_sdspi_host(SPI2_HOST, &sdspi_host);
    api_test_check_bool(CAPI, "bsp_sdcard_get_sdspi_host", API_ASPECT_NORMAL,
                        sdspi_host.slot == SPI2_HOST, "generic SDSPI host populated");
    bsp_sdcard_sdspi_get_slot(SPI2_HOST, &sdspi);
    api_test_check_bool(CAPI, "bsp_sdcard_sdspi_get_slot", API_ASPECT_NORMAL,
                        sdspi.host_id == SPI2_HOST && sdspi.gpio_cs == GPIO_NUM_NC,
                        "SDSPI placeholder populated");
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, NULL);
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, NULL);
    bsp_sdcard_get_sdspi_host(SPI2_HOST, NULL);
    bsp_sdcard_sdspi_get_slot(SPI2_HOST, NULL);
    api_test_log(CAPI, "bsp_sdcard_get_sdmmc_host", API_ASPECT_BOUNDARY, API_PASS,
                 "NULL output is ignored");
    api_test_log(CAPI, "bsp_sdcard_sdmmc_get_slot", API_ASPECT_BOUNDARY, API_PASS,
                 "NULL output is ignored");
    api_test_log(CAPI, "bsp_sdcard_get_sdspi_host", API_ASPECT_BOUNDARY, API_PASS,
                 "NULL output is ignored");
    api_test_log(CAPI, "bsp_sdcard_sdspi_get_slot", API_ASPECT_BOUNDARY, API_PASS,
                 "NULL output is ignored");

    api_test_require(CAPI, "candis_storage_mount", API_ASPECT_BOUNDARY,
                     candis_storage_mount(NULL, false, NULL), ESP_ERR_INVALID_ARG,
                     "NULL base path and output");
    api_test_require(CAPI, "candis_storage_mount_with_config", API_ASPECT_BOUNDARY,
                     candis_storage_mount_with_config("/sdcard", NULL, NULL),
                     ESP_ERR_INVALID_ARG, "NULL mount config");
    sdmmc_card_t *card = NULL;
    api_test_require(CAPI, "candis_storage_mount_with_config", API_ASPECT_BOUNDARY,
                     candis_storage_mount_with_config("", &mount_config, &card),
                     ESP_ERR_INVALID_ARG, "relative VFS path");
    api_test_require(CAPI, "candis_storage_mount_with_config", API_ASPECT_BOUNDARY,
                     candis_storage_mount_with_config("/sdcard", &mount_config, NULL),
                     ESP_ERR_INVALID_ARG, "NULL card output");
    api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_ERROR,
                     candis_storage_check_card(), ESP_ERR_INVALID_STATE, "no active mount");
    api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_LIFECYCLE,
                     candis_storage_unmount(), ESP_OK, "unmount without mount is safe");
    api_test_check_bool(CAPI, "bsp_sdcard_get_handle", API_ASPECT_ERROR,
                        bsp_sdcard_get_handle() == NULL, "no BSP card handle while unmounted");
    api_test_require(CAPI, "bsp_sdcard_unmount", API_ASPECT_LIFECYCLE,
                     bsp_sdcard_unmount(), ESP_OK, "repeated BSP unmount is safe");
    api_test_require(CAPI, "bsp_sdcard_sdmmc_mount", API_ASPECT_BOUNDARY,
                     bsp_sdcard_sdmmc_mount(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    api_test_require(CAPI, "bsp_sdcard_sdspi_mount", API_ASPECT_BOUNDARY,
                     bsp_sdcard_sdspi_mount(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    bsp_sdcard_cfg_t sdspi_cfg = {0};
    api_test_require(CAPI, "bsp_sdcard_sdspi_mount", API_ASPECT_ERROR,
                     bsp_sdcard_sdspi_mount(&sdspi_cfg), ESP_ERR_NOT_SUPPORTED,
                     "SDSPI mount intentionally unsupported");

    api_test_require(CAPI, "candis_storage_detect_read", API_ASPECT_BOUNDARY,
                     candis_storage_detect_read(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    api_test_require(CAPI, "candis_storage_detect_start", API_ASPECT_NORMAL,
                     candis_storage_detect_start(true), ESP_OK, "claim GPIO39 detect and DC1SW");
    bool present = false;
    api_test_require(CAPI, "candis_storage_detect_read", API_ASPECT_NORMAL,
                     candis_storage_detect_read(&present), ESP_OK, "read raw card switch");
    api_test_emit("API_STORAGE_PROBE|card_detect=%s|gpio=39\n", present ? "present" : "absent");
    api_test_require(CAPI, "candis_storage_detect_stop", API_ASPECT_NORMAL,
                     candis_storage_detect_stop(), ESP_OK, "release detect GPIO");
    api_test_require(CAPI, "candis_storage_detect_stop", API_ASPECT_LIFECYCLE,
                     candis_storage_detect_stop(), ESP_OK, "repeated detect release");

    esp_vfs_fat_sdmmc_mount_config_t fat = {.max_files = 4};
    bsp_sdcard_cfg_t bsp_cfg = {
        .mount = &fat,
        .host = &host,
        .slot.sdmmc = &slot,
    };
    if (present) {
    api_test_require(CAPI, "bsp_sdcard_sdmmc_mount", API_ASPECT_NORMAL,
                     bsp_sdcard_sdmmc_mount(&bsp_cfg), ESP_OK,
                     "mount formal Dock TF card through caller SDMMC config");
    if (bsp_sdcard_get_handle()) {
        api_test_require(CAPI, "bsp_sdcard_sdmmc_mount", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "SDMMC card is active");
        api_test_require(CAPI, "bsp_sdcard_unmount", API_ASPECT_NORMAL,
                         bsp_sdcard_unmount(), ESP_OK, "unmount caller-configured card");
    }

    api_test_require(CAPI, "bsp_sdcard_mount", API_ASPECT_NORMAL,
                     bsp_sdcard_mount(), ESP_OK, "mount configured BSP TF card");
    if (bsp_sdcard_get_handle()) {
        api_test_require(CAPI, "bsp_sdcard_mount", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "BSP card is active");
        api_test_require(CAPI, "bsp_sdcard_unmount", API_ASPECT_NORMAL,
                         bsp_sdcard_unmount(), ESP_OK, "unmount BSP card");
    }

    card = NULL;
    api_test_require(CAPI, "candis_storage_mount", API_ASPECT_NORMAL,
                     candis_storage_mount("/sdcard", true, &card), ESP_OK,
                     "mount TF card with public storage API");
    if (card) {
        api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "public storage card is active");
        api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_NORMAL,
                         candis_storage_unmount(), ESP_OK, "unmount public storage card");
    }

    card = NULL;
    mount_config.manage_power = true;
    api_test_require(CAPI, "candis_storage_mount_with_config", API_ASPECT_NORMAL,
                     candis_storage_mount_with_config("/sdcard", &mount_config, &card), ESP_OK,
                     "mount TF card with explicit 6.2 write policy");
    if (card) {
        api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "configured storage card is active");
        api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_NORMAL,
                         candis_storage_unmount(), ESP_OK, "unmount configured storage card");
    }
    } else {
        API_BLOCKED(CAPI, "bsp_sdcard_mount", API_ASPECT_HARDWARE,
                    "Dock card detect reports no TF card");
        API_BLOCKED(CAPI, "bsp_sdcard_sdmmc_mount", API_ASPECT_HARDWARE,
                    "Dock card detect reports no TF card");
        API_BLOCKED(CAPI, "candis_storage_mount", API_ASPECT_HARDWARE,
                    "Dock card detect reports no TF card");
        API_BLOCKED(CAPI, "candis_storage_mount_with_config", API_ASPECT_HARDWARE,
                    "Dock card detect reports no TF card");
    }

    candis_storage_reset_config();
    api_test_log(CAPI, "candis_storage_reset_config", API_ASPECT_NORMAL, API_PASS,
                 "runtime slot configuration cleared");
    api_test_require(CAPI, "candis_storage_detect_start", API_ASPECT_ERROR,
                     candis_storage_detect_start(false), ESP_ERR_INVALID_STATE,
                     "detect requires configuration");
}

static void test_motor(void)
{
    candis_motor_state_t state = {0};
    api_test_require(CAPI, "candis_motor_init", API_ASPECT_BOUNDARY,
                     candis_motor_init(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    candis_motor_config_t invalid = CANDIS_MOTOR_CONFIG_DEFAULT();
    invalid.amplitude_min_duty_percent = 0;
    api_test_require(CAPI, "candis_motor_init", API_ASPECT_BOUNDARY,
                     candis_motor_init(&invalid), ESP_ERR_INVALID_ARG,
                     "zero minimum duty");
    invalid = (candis_motor_config_t)CANDIS_MOTOR_CONFIG_DEFAULT();
    invalid.amplitude_max_duty_percent = 101;
    api_test_require(CAPI, "candis_motor_init", API_ASPECT_BOUNDARY,
                     candis_motor_init(&invalid), ESP_ERR_INVALID_ARG,
                     "maximum duty above 100");
    invalid = (candis_motor_config_t)CANDIS_MOTOR_CONFIG_DEFAULT();
    invalid.frequency_hz = 0;
    api_test_require(CAPI, "candis_motor_init", API_ASPECT_BOUNDARY,
                     candis_motor_init(&invalid), ESP_ERR_INVALID_ARG, "zero PWM frequency");

    api_test_check_bool(CAPI, "candis_motor_amplitude_to_duty", API_ASPECT_NORMAL,
                        candis_motor_amplitude_to_duty(0) == 0 &&
                        candis_motor_amplitude_to_duty(100) == 0,
                        "unconfigured mapping returns zero");
    api_test_check_bool(CAPI, "candis_motor_duty_to_amplitude", API_ASPECT_NORMAL,
                        candis_motor_duty_to_amplitude(0) == 0,
                        "unconfigured inverse mapping returns zero");
    api_test_require(CAPI, "candis_motor_start_amplitude", API_ASPECT_ERROR,
                     candis_motor_start_amplitude(0), ESP_ERR_INVALID_ARG,
                     "zero amplitude rejected");
    api_test_require(CAPI, "candis_motor_start_amplitude", API_ASPECT_ERROR,
                     candis_motor_start_amplitude(50), ESP_ERR_INVALID_STATE,
                     "start before init rejected");
    api_test_require(CAPI, "candis_motor_set_amplitude", API_ASPECT_ERROR,
                     candis_motor_set_amplitude(50), ESP_ERR_INVALID_STATE,
                     "set before init rejected");
    api_test_require(CAPI, "candis_motor_set_frequency", API_ASPECT_ERROR,
                     candis_motor_set_frequency(20000), ESP_ERR_INVALID_STATE,
                     "frequency before init rejected");
    api_test_require(CAPI, "candis_motor_start", API_ASPECT_ERROR,
                     candis_motor_start(50), ESP_ERR_INVALID_STATE,
                     "raw start before init rejected");
    api_test_require(CAPI, "candis_motor_set_duty", API_ASPECT_ERROR,
                     candis_motor_set_duty(50), ESP_ERR_INVALID_STATE,
                     "raw duty before init rejected");
    api_test_require(CAPI, "candis_motor_pulse", API_ASPECT_ERROR,
                     candis_motor_pulse(50, 10), ESP_ERR_INVALID_STATE,
                     "pulse before init rejected");
    api_test_require(CAPI, "candis_motor_pulse_amplitude", API_ASPECT_ERROR,
                     candis_motor_pulse_amplitude(50, 10), ESP_ERR_INVALID_STATE,
                     "amplitude pulse before init rejected");
    api_test_require(CAPI, "candis_motor_get_state", API_ASPECT_BOUNDARY,
                     candis_motor_get_state(NULL), ESP_ERR_INVALID_ARG, "NULL state");
    api_test_require(CAPI, "candis_motor_get_state", API_ASPECT_NORMAL,
                     candis_motor_get_state(&state), ESP_OK, "unconfigured state query");
    api_test_check_bool(CAPI, "candis_motor_get_state", API_ASPECT_NORMAL,
                        !state.active && !state.cleanup_pending, "inactive clean state");
    api_test_require(CAPI, "candis_motor_stop", API_ASPECT_LIFECYCLE,
                     candis_motor_stop(), ESP_OK, "stop before init is safe");
    API_BLOCKED(CAPI, "candis_motor_init", API_ASPECT_HARDWARE,
                "physical vibration requires a confirmed motor and supply");
    API_BLOCKED(CAPI, "candis_motor_pulse_amplitude", API_ASPECT_HARDWARE,
                "no automated vibration sensor is attached");
}

static void test_usb(void)
{
    uint8_t byte = 0;
    size_t count = 0;
    char message[] = "api";
    esp_err_t start_normal = ESP_ERR_INVALID_STATE;
    esp_err_t start_repeat = ESP_ERR_INVALID_STATE;
    esp_err_t stop_normal = ESP_ERR_INVALID_STATE;
    esp_err_t stop_repeat = ESP_ERR_INVALID_STATE;
    esp_err_t host_start = ESP_ERR_INVALID_STATE;
    esp_err_t host_start_repeat = ESP_ERR_INVALID_STATE;
    esp_err_t host_stop = ESP_ERR_INVALID_STATE;
    esp_err_t host_stop_repeat = ESP_ERR_INVALID_STATE;
    esp_err_t console_stop = ESP_ERR_INVALID_STATE;
    bool read_normal = false;
    bool write_normal = false;

    api_test_require(CAPI, "candis_usb_device_read", API_ASPECT_BOUNDARY,
                     candis_usb_device_read(NULL, 1, &count), ESP_ERR_INVALID_ARG, "NULL buffer");
    api_test_require(CAPI, "candis_usb_device_read", API_ASPECT_BOUNDARY,
                     candis_usb_device_read(&byte, 0, &count), ESP_ERR_INVALID_ARG, "zero capacity");
    api_test_require(CAPI, "candis_usb_device_read", API_ASPECT_BOUNDARY,
                     candis_usb_device_read(&byte, 1, NULL), ESP_ERR_INVALID_ARG, "NULL count");
    api_test_require(CAPI, "candis_usb_device_write", API_ASPECT_BOUNDARY,
                     candis_usb_device_write(NULL, 1, &count), ESP_ERR_INVALID_ARG, "NULL buffer");
    api_test_require(CAPI, "candis_usb_device_write", API_ASPECT_BOUNDARY,
                     candis_usb_device_write(message, 0, &count), ESP_ERR_INVALID_ARG, "zero bytes");
    api_test_require(CAPI, "candis_usb_device_write", API_ASPECT_BOUNDARY,
                     candis_usb_device_write(message, 3, NULL), ESP_ERR_INVALID_ARG, "NULL count");
    api_test_require(CAPI, "bsp_usb_host_start", API_ASPECT_BOUNDARY,
                     bsp_usb_host_start((bsp_usb_host_power_mode_t)1, false),
                     ESP_ERR_INVALID_ARG, "invalid power mode");
    api_test_require(CAPI, "bsp_usb_host_start", API_ASPECT_ERROR,
                     bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_AUTO, true),
                     ESP_ERR_NOT_SUPPORTED, "500 mA limit unsupported");

    console_stop = api_test_console_stop();
    start_normal = candis_usb_device_start();
    if (start_normal == ESP_OK) {
        start_repeat = candis_usb_device_start();
        for (unsigned attempt = 0; attempt < 60 && !read_normal; ++attempt) {
            esp_err_t read_err = candis_usb_device_read(&byte, 1, &count);
            read_normal = read_err == ESP_OK && count == 0;
            if (!read_normal) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        for (unsigned attempt = 0; attempt < 60 && !write_normal; ++attempt) {
            esp_err_t write_err = candis_usb_device_write(message, 3, &count);
            write_normal = write_err == ESP_OK && count > 0;
            if (!write_normal) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        stop_normal = candis_usb_device_stop();
        stop_repeat = candis_usb_device_stop();
    }

    if (start_normal == ESP_OK && stop_normal == ESP_OK) {
        host_start = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_AUTO, false);
        if (host_start == ESP_OK) {
            host_start_repeat = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_AUTO, false);
            host_stop = bsp_usb_host_stop();
            host_stop_repeat = bsp_usb_host_stop();
        }
    }

    const esp_err_t console = api_test_console_restore(15000);
    api_test_require("api-test", "stop_capture_console", API_ASPECT_LIFECYCLE,
                     console_stop, ESP_OK, "release capture CDC before native USB tests");
    api_test_check_bool(CAPI, "candis_usb_device_read", API_ASPECT_NORMAL, read_normal,
                        "nonblocking read reports zero bytes when no input is pending");
    api_test_check_bool(CAPI, "candis_usb_device_write", API_ASPECT_NORMAL, write_normal,
                        "capture host receives queued device data");
    api_test_require(CAPI, "candis_usb_device_start", API_ASPECT_NORMAL, start_normal, ESP_OK,
                     "install TinyUSB CDC device");
    api_test_require(CAPI, "candis_usb_device_start", API_ASPECT_LIFECYCLE, start_repeat, ESP_OK,
                     "second start is idempotent");
    api_test_require(CAPI, "candis_usb_device_stop", API_ASPECT_NORMAL, stop_normal, ESP_OK,
                     "release CDC and PHY");
    api_test_require(CAPI, "candis_usb_device_stop", API_ASPECT_LIFECYCLE, stop_repeat, ESP_OK,
                     "repeated stop");
    api_test_require(CAPI, "bsp_usb_host_start", API_ASPECT_NORMAL, host_start, ESP_OK,
                     "start USB Host after stopping Device");
    api_test_require(CAPI, "bsp_usb_host_start", API_ASPECT_LIFECYCLE, host_start_repeat, ESP_OK,
                     "repeated host start is idempotent");
    api_test_require(CAPI, "bsp_usb_host_stop", API_ASPECT_NORMAL, host_stop, ESP_OK,
                     "stop host stack and PHY");
    api_test_require(CAPI, "bsp_usb_host_stop", API_ASPECT_LIFECYCLE, host_stop_repeat, ESP_OK,
                     "repeated host stop");
    api_test_require(CAPI, "candis_usb_device_start", API_ASPECT_NORMAL, console, ESP_OK,
                     "restore CDC capture channel after USB API tests");
    API_BLOCKED(CAPI, "bsp_usb_host_start", API_ASPECT_HARDWARE,
                "no USB peripheral attached to the host port");
    API_BLOCKED(CAPI, "bsp_usb_host_stop", API_ASPECT_HARDWARE,
                "host traffic was not exercised without an attached USB peripheral");
}

static void test_imu(void)
{
    candis_imu_config_t config = CANDIS_IMU_CONFIG_DEFAULT();
    candis_capability_state_t state = CANDIS_CAPABILITY_UNSUPPORTED;
    candis_imu_sample_t sample;
    float temperature = 0;
    api_test_require(CAPI, "candis_imu_start_with_config", API_ASPECT_BOUNDARY,
                     candis_imu_start_with_config(NULL), ESP_ERR_INVALID_ARG, "NULL config");
    api_test_require(CAPI, "candis_imu_probe", API_ASPECT_BOUNDARY,
                     candis_imu_probe(NULL, &state), ESP_ERR_INVALID_ARG, "NULL config");
    api_test_require(CAPI, "candis_imu_get_state", API_ASPECT_BOUNDARY,
                     candis_imu_get_state(NULL), ESP_ERR_INVALID_ARG, "NULL state");
    api_test_require(CAPI, "candis_imu_read", API_ASPECT_BOUNDARY,
                     candis_imu_read(NULL), ESP_ERR_INVALID_ARG, "NULL sample");
    api_test_require(CAPI, "candis_imu_read_temperature", API_ASPECT_BOUNDARY,
                     candis_imu_read_temperature(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    api_test_require(CAPI, "candis_imu_suspend", API_ASPECT_ERROR,
                     candis_imu_suspend(), ESP_ERR_INVALID_STATE, "suspend before start");
    api_test_require(CAPI, "candis_imu_resume", API_ASPECT_ERROR,
                     candis_imu_resume(), ESP_ERR_INVALID_STATE, "resume before start");
    api_test_require(CAPI, "candis_imu_stop", API_ASPECT_LIFECYCLE,
                     candis_imu_stop(), ESP_OK, "stop before start is safe");

    candis_expansion_power_handle_t lease = NULL;
    esp_err_t err = candis_expansion_power_claim(true, &lease);
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_handle_t scan_bus = NULL;
    bool bus_is_expansion = false;
    if (err == ESP_OK) {
        const i2c_master_bus_config_t bus_config = {
            .i2c_port = I2C_NUM_1,
            .scl_io_num = GPIO_NUM_58,
            .sda_io_num = GPIO_NUM_59,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = 1,
        };
        err = i2c_new_master_bus(&bus_config, &scan_bus);
    }
    if (err == ESP_OK && scan_bus) {
        const uint8_t expected_addresses[] = {0x50, 0x51, 0x52, 0x68, 0x69};
        uint32_t responding = 0;
        const esp_err_t scan = candis_i2c_probe_addresses(scan_bus, expected_addresses,
                                                          sizeof(expected_addresses) /
                                                          sizeof(expected_addresses[0]),
                                                          &responding);
        api_test_emit("API_DEVICE_SCAN|bus=expansion|version=%s|result=%s|responding=0x%08" PRIx32
                      "|addresses=0x50,0x51,0x52,0x68,0x69\n", api_test_idf(),
                      esp_err_to_name(scan), responding);
        if (scan == ESP_OK && (responding & UINT32_C(1))) {
            const i2c_device_config_t descriptor = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = 0x50,
                .scl_speed_hz = 100000,
            };
            i2c_master_dev_handle_t eeprom = NULL;
            esp_err_t read = i2c_master_bus_add_device(scan_bus, &descriptor, &eeprom);
            uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE] = {0};
            if (read == ESP_OK) {
                const uint8_t offset = 0;
                read = i2c_master_transmit_receive(eeprom, &offset, 1, image, sizeof(image), 100);
                if (read == ESP_OK) {
                    char dump[65 * 3] = {0};
                    for (size_t i = 0; i < 64; ++i) {
                        snprintf(dump + i * 3, sizeof(dump) - i * 3, "%02x%s",
                                 image[i], i == 63 ? "" : ":");
                    }
                    api_test_emit("API_EEPROM_DUMP|address=0x50|length=134|head=%s\n", dump);
                }
                (void)i2c_master_bus_rm_device(eeprom);
            }
            if (read != ESP_OK) {
                api_test_emit("API_EEPROM_DUMP|address=0x50|read=%s\n", esp_err_to_name(read));
            }
        }
        if (scan == ESP_OK && (responding & (UINT32_C(1) << 3 | UINT32_C(1) << 4))) {
            bus = scan_bus;
            scan_bus = NULL;
            bus_is_expansion = true;
        } else {
            err = i2c_del_master_bus(scan_bus);
            scan_bus = NULL;
        }
    }
    if (err == ESP_OK && !bus) {
        err = bsp_i2c_init();
        bus = bsp_i2c_get_handle();
    }
    if (err == ESP_OK && bus) {
        /* The integrated test baseboard places BMI270 on the core bus, while
         * production Dock hardware places it on the expansion bus. candis_imu
         * accepts either caller-owned bus without changing the board contract. */
        config.i2c_bus = bus;
        config.i2c_address = 0x68;
        err = candis_imu_probe(&config, &state);
        if (err != ESP_OK) {
            api_test_check_bool(CAPI, "candis_imu_probe", API_ASPECT_ERROR, err != ESP_OK,
                                "probe reports an unavailable expansion bus");
            API_BLOCKED(CAPI, "candis_imu_probe", API_ASPECT_HARDWARE,
                        bus_is_expansion ? "BMI270 did not respond on the Dock expansion bus"
                                         : "BMI270 did not respond on the fixture core bus");
            API_BLOCKED(CAPI, "candis_imu_start_with_config", API_ASPECT_HARDWARE,
                        "no usable BMI270 is attached");
            API_BLOCKED(CAPI, "candis_imu_suspend", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_resume", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_read", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_read_temperature", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
        } else if (state == CANDIS_CAPABILITY_PRESENT) {
            err = candis_imu_start_with_config(&config);
            api_test_require(CAPI, "candis_imu_start_with_config", API_ASPECT_NORMAL,
                             err, ESP_OK, "start measurements on BMI270");
            if (err == ESP_OK) {
                api_test_require(CAPI, "candis_imu_get_state", API_ASPECT_NORMAL,
                                 candis_imu_get_state(&state), ESP_OK, "active state");
                api_test_check_bool(CAPI, "candis_imu_get_state", API_ASPECT_NORMAL,
                                    state == CANDIS_CAPABILITY_INITIALIZED,
                                    "state is INITIALIZED");
                vTaskDelay(pdMS_TO_TICKS(30));
                for (unsigned attempt = 0; attempt < 5; ++attempt) {
                    err = candis_imu_read(&sample);
                    if (err != ESP_ERR_NOT_FINISHED) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                api_test_require(CAPI, "candis_imu_read", API_ASPECT_NORMAL, err, ESP_OK,
                                 "six-axis burst with sensor time");
                if (err == ESP_OK) {
                    api_test_check_bool(CAPI, "candis_imu_read", API_ASPECT_NORMAL,
                                        sample.timestamp_us > 0 &&
                                        fabsf(sample.acceleration_g[0]) < 20.0f &&
                                        fabsf(sample.angular_velocity_dps[0]) < 3000.0f,
                                        "sample fields in configured ranges");
                }
                err = candis_imu_read_temperature(&temperature);
                api_test_check_bool(CAPI, "candis_imu_read_temperature", API_ASPECT_NORMAL,
                                    err == ESP_OK || err == ESP_ERR_NOT_FINISHED,
                                    err == ESP_OK ? "temperature read" : "conversion pending");
                api_test_require(CAPI, "candis_imu_suspend", API_ASPECT_NORMAL,
                                 candis_imu_suspend(), ESP_OK, "enter advanced power save");
                api_test_require(CAPI, "candis_imu_resume", API_ASPECT_NORMAL,
                                 candis_imu_resume(), ESP_OK, "resume sampling");
                api_test_require(CAPI, "candis_imu_stop", API_ASPECT_NORMAL,
                                 candis_imu_stop(), ESP_OK, "stop and remove BMI270");
                api_test_require(CAPI, "candis_imu_stop", API_ASPECT_LIFECYCLE,
                                 candis_imu_stop(), ESP_OK, "repeated stop");
            }
        } else {
            api_test_check_bool(CAPI, "candis_imu_probe", API_ASPECT_ERROR,
                                state == CANDIS_CAPABILITY_NOT_PRESENT ||
                                state == CANDIS_CAPABILITY_UNSUPPORTED,
                                state == CANDIS_CAPABILITY_UNSUPPORTED
                                    ? "address responds but chip ID is not BMI270"
                                    : "BMI270 not present");
            api_test_require(CAPI, "candis_imu_stop", API_ASPECT_LIFECYCLE,
                             candis_imu_stop(), ESP_OK, "cleanup after probe-only path");
            API_BLOCKED(CAPI, "candis_imu_start_with_config", API_ASPECT_HARDWARE,
                        "no usable BMI270 is attached");
            API_BLOCKED(CAPI, "candis_imu_suspend", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_resume", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_read", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
            API_BLOCKED(CAPI, "candis_imu_read_temperature", API_ASPECT_HARDWARE,
                        "BMI270 was not started");
        }
    } else {
        API_BLOCKED(CAPI, "candis_imu_probe", API_ASPECT_HARDWARE,
                    "BMI270 test bus unavailable");
    }
    if (bus) {
        if (bus_is_expansion) {
            (void)i2c_del_master_bus(bus);
        } else {
            (void)bsp_i2c_deinit();
        }
    }
    if (scan_bus) {
        (void)i2c_del_master_bus(scan_bus);
    }
    if (lease) {
        (void)candis_expansion_power_unclaim(&lease);
    }
}

void api_tests_candis_peripherals(void)
{
    test_storage();
    test_motor();
    test_usb();
    test_imu();
}
