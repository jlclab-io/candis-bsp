/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "api_suites.h"
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "bsp/candis_camera_profile.h"
#include "bsp/candis_dock.h"
#include "bsp/candis_power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DAPI "candis-dock"
#define CAPI "candis"

static void test_speaker_mapping(const candis_baseboard_info_t *identity)
{
    candis_dock_speaker_channel_t channel = (candis_dock_speaker_channel_t)99;
    candis_baseboard_info_t bad = {0};
    api_test_require(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_BOUNDARY,
                     candis_dock_get_speaker_channel(NULL, &channel), ESP_ERR_INVALID_ARG,
                     "NULL identity");
    bad.kind = (candis_baseboard_kind_t)1;
    bad.hardware_revision = 0x0102;
    api_test_require(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_ERROR,
                     candis_dock_get_speaker_channel(&bad, &channel), ESP_ERR_INVALID_ARG,
                     "wrong baseboard kind");
    bad = *identity;
    bad.hardware_revision = 0x0100;
    api_test_require(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_ERROR,
                     candis_dock_get_speaker_channel(&bad, &channel), ESP_ERR_NOT_SUPPORTED,
                     "unsupported hardware revision");
    api_test_require(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_BOUNDARY,
                     candis_dock_get_speaker_channel(identity, NULL), ESP_ERR_INVALID_ARG,
                     "NULL channel output");
    esp_err_t err = candis_dock_get_speaker_channel(identity, &channel);
    api_test_require(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_NORMAL,
                     err, ESP_OK, "verified Dock revision");
    if (err == ESP_OK) {
        api_test_check_bool(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_HARDWARE,
                            channel == (identity->hardware_revision == 0x0101
                                            ? CANDIS_DOCK_SPEAKER_LEFT
                                            : CANDIS_DOCK_SPEAKER_RIGHT),
                            "revision maps to CN3 left/right PCM slot");
    }
}

static bool verify_card_io(void)
{
    static const char payload[] = "CANDIS_DOCK_API_IO\n";
    char readback[sizeof(payload)] = {0};
    const char *path = "/sdcard/candis_dock_api_io.txt";
    FILE *file = fopen(path, "wb");
    if (!file) {
        return false;
    }
    const bool written = fwrite(payload, 1, sizeof(payload), file) == sizeof(payload);
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        (void)remove(path);
        return false;
    }
    file = fopen(path, "rb");
    if (!file) {
        (void)remove(path);
        return false;
    }
    const bool read = fread(readback, 1, sizeof(readback), file) == sizeof(readback);
    const bool read_closed = fclose(file) == 0;
    const bool removed = remove(path) == 0;
    return read && read_closed && removed && memcmp(payload, readback, sizeof(payload)) == 0;
}

static void mount_attempt(const candis_baseboard_info_t *identity)
{
    sdmmc_card_t *card = NULL;
    esp_err_t err = candis_dock_mount(identity, "/sdcard", &card);
    if (err == ESP_OK) {
        api_test_check_bool(DAPI, "candis_dock_mount", API_ASPECT_NORMAL,
                            card != NULL && verify_card_io(),
                            "card handle and FAT write/read/delete round trip valid");
        api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "card detect reports inserted");
        api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_NORMAL,
                         candis_storage_unmount(), ESP_OK, "unmount card");
    } else {
        api_test_require(DAPI, "candis_dock_mount", API_ASPECT_ERROR, err,
                         ESP_ERR_NOT_FOUND, "no card is not a crash");
        API_BLOCKED(DAPI, "candis_dock_mount", API_ASPECT_HARDWARE,
                    "TF card absent or not mountable");
        API_BLOCKED(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                    "no active card mount");
    }

    candis_storage_mount_config_t config = {.manage_power = false, .read_only = false};
    err = candis_dock_mount_with_power(identity, "/sdcard", false, &card);
    if (err == ESP_OK) {
        api_test_check_bool(DAPI, "candis_dock_mount_with_power", API_ASPECT_NORMAL,
                            card != NULL, "card handle returned with existing-rail policy");
        api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "power-policy card is active");
        api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_NORMAL,
                         candis_storage_unmount(), ESP_OK, "unmount second mount");
    } else {
        API_BLOCKED(DAPI, "candis_dock_mount_with_power", API_ASPECT_HARDWARE,
                    "TF card absent or not mountable");
    }
    err = candis_dock_mount_with_config(identity, "/sdcard", &config, &card);
    if (err == ESP_OK) {
        api_test_check_bool(DAPI, "candis_dock_mount_with_config", API_ASPECT_NORMAL,
                            card != NULL, "card handle returned by configured mount");
        api_test_require(CAPI, "candis_storage_check_card", API_ASPECT_HARDWARE,
                         candis_storage_check_card(), ESP_OK, "configured card is active");
        api_test_require(CAPI, "candis_storage_unmount", API_ASPECT_NORMAL,
                         candis_storage_unmount(), ESP_OK, "unmount configured mount");
    } else {
        API_BLOCKED(DAPI, "candis_dock_mount_with_config", API_ASPECT_HARDWARE,
                    "TF card absent or not mountable");
    }
    api_test_require(DAPI, "candis_dock_mount_with_config", API_ASPECT_BOUNDARY,
                     candis_dock_mount_with_config(identity, "/sdcard", NULL, &card),
                     ESP_ERR_INVALID_ARG, "NULL mount config");
}

void api_tests_dock(void)
{
    api_test_suite_begin(DAPI);
    candis_capability_state_t state = CANDIS_CAPABILITY_UNSUPPORTED;
    candis_baseboard_info_t identity = {0};
    candis_baseboard_info_t unpowered = {0};

    api_test_require(DAPI, "candis_dock_probe", API_ASPECT_BOUNDARY,
                     candis_dock_probe(NULL), ESP_ERR_INVALID_ARG, "NULL identity");
    api_test_require(DAPI, "candis_dock_get_state", API_ASPECT_BOUNDARY,
                     candis_dock_get_state(NULL, &identity), ESP_ERR_INVALID_ARG,
                     "NULL state");
    candis_expansion_power_handle_t fixture_power = NULL;
    const esp_err_t fixture_power_err = candis_expansion_power_claim(true, &fixture_power);
    api_test_require(DAPI, "candis_expansion_power_claim", API_ASPECT_NORMAL,
                     fixture_power_err, ESP_OK, "enable DC1SW for connected Dock fixture");
    esp_err_t err = candis_dock_get_state(&state, &unpowered);
    if (err == ESP_OK) {
        api_test_require(DAPI, "candis_dock_get_state", API_ASPECT_NORMAL, err, ESP_OK,
                         "classify Dock before open");
        api_test_check_bool(DAPI, "candis_dock_get_state", API_ASPECT_NORMAL,
                            state == CANDIS_CAPABILITY_NOT_PRESENT ||
                            state == CANDIS_CAPABILITY_PRESENT ||
                            state == CANDIS_CAPABILITY_UNSUPPORTED,
                            "state is one defined classification");
    } else if (err == ESP_ERR_INVALID_STATE) {
        api_test_require(DAPI, "candis_dock_get_state", API_ASPECT_ERROR, err,
                         ESP_ERR_INVALID_STATE, "UART0 is not released for the Dock bus");
        API_BLOCKED(DAPI, "candis_dock_get_state", API_ASPECT_HARDWARE,
                    "Dock classification requires the application to release UART0");
    } else {
        api_test_check_bool(DAPI, "candis_dock_get_state", API_ASPECT_ERROR, false,
                            "unexpected Dock classification error");
    }
    api_test_require(DAPI, "candis_dock_open", API_ASPECT_BOUNDARY,
                     candis_dock_open(false, NULL), ESP_ERR_INVALID_ARG, "NULL identity");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_BOUNDARY,
                     candis_dock_read_eeprom(NULL, 1), ESP_ERR_INVALID_ARG, "NULL buffer");
    uint8_t eeprom[256];
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_BOUNDARY,
                     candis_dock_read_eeprom(eeprom, 0), ESP_ERR_INVALID_ARG, "zero length");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_BOUNDARY,
                     candis_dock_read_eeprom(eeprom, sizeof(eeprom) + 1), ESP_ERR_INVALID_ARG,
                     "length above EEPROM size");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_ERROR,
                     candis_dock_read_eeprom(eeprom, 1), ESP_ERR_INVALID_STATE,
                     "read before open");
    api_test_require(DAPI, "candis_dock_close", API_ASPECT_LIFECYCLE,
                     candis_dock_close(), ESP_OK, "close before open");

    err = candis_dock_open(true, &identity);
    if (err != ESP_OK) {
        char failure[96];
        snprintf(failure, sizeof(failure), "Dock open failed as expected: %s", esp_err_to_name(err));
        api_test_check_bool(DAPI, "candis_dock_open", API_ASPECT_ERROR,
                            err != ESP_OK, failure);
        API_BLOCKED(DAPI, "candis_dock_open", API_ASPECT_HARDWARE,
                    "Candis Dock not attached or DC1SW probe failed");
        API_BLOCKED(DAPI, "candis_dock_read_eeprom", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_mount", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_mount_with_power", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_mount_with_config", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_detect_start", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_probe_modules", API_ASPECT_HARDWARE,
                    "Dock session unavailable");
        API_BLOCKED(DAPI, "candis_dock_get_speaker_channel", API_ASPECT_HARDWARE,
                    "Dock identity unavailable");
        (void)candis_dock_close();
        api_test_suite_end(DAPI);
        return;
    }

    api_test_require(DAPI, "candis_dock_open", API_ASPECT_NORMAL, err, ESP_OK,
                     "DC1SW lease and Dock descriptor validated");
    api_test_check_bool(DAPI, "candis_dock_open", API_ASPECT_HARDWARE,
                        identity.kind == CANDIS_BASEBOARD_BB &&
                        (identity.hardware_revision == 0x0101 || identity.hardware_revision == 0x0102),
                        "Dock identity tuple is supported");
    api_test_require(DAPI, "candis_dock_open", API_ASPECT_LIFECYCLE,
                     candis_dock_open(true, &unpowered), ESP_ERR_INVALID_STATE,
                     "second open rejected");
    api_test_require(DAPI, "candis_dock_get_state", API_ASPECT_NORMAL,
                     candis_dock_get_state(&state, &unpowered), ESP_OK, "query active session");
    api_test_check_bool(DAPI, "candis_dock_get_state", API_ASPECT_NORMAL,
                        state == CANDIS_CAPABILITY_INITIALIZED &&
                        memcmp(&identity, &unpowered, sizeof(identity)) == 0,
                        "state INITIALIZED and identity copied");
    api_test_require(DAPI, "candis_dock_probe", API_ASPECT_NORMAL,
                     candis_dock_probe(&unpowered), ESP_OK, "probe over active private bus");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_NORMAL,
                     candis_dock_read_eeprom(eeprom, CANDIS_BASEBOARD_IMAGE_SIZE), ESP_OK,
                     "read descriptor image");
    candis_baseboard_info_t decoded = {0};
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_HARDWARE,
                     candis_baseboard_decode(eeprom, CANDIS_BASEBOARD_IMAGE_SIZE, &decoded),
                     ESP_OK, "decode descriptor read from physical EEPROM");
    api_test_check_bool(CAPI, "candis_baseboard_decode", API_ASPECT_HARDWARE,
                        decoded.kind == identity.kind && decoded.hardware_revision == identity.hardware_revision &&
                        decoded.format_revision == identity.format_revision,
                        "EEPROM image and probe identity agree");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_NORMAL,
                     candis_dock_read_eeprom(eeprom, sizeof(eeprom)), ESP_OK,
                     "read full 256-byte EEPROM");
    test_speaker_mapping(&identity);

    candis_dock_module_scan_t scan = {0};
    api_test_require(DAPI, "candis_dock_probe_modules", API_ASPECT_BOUNDARY,
                     candis_dock_probe_modules(NULL, &scan), ESP_ERR_INVALID_ARG,
                     "NULL identity");
    api_test_require(DAPI, "candis_dock_probe_modules", API_ASPECT_BOUNDARY,
                     candis_dock_probe_modules(&identity, NULL), ESP_ERR_INVALID_ARG,
                     "NULL scan output");
    api_test_require(DAPI, "candis_dock_probe_modules", API_ASPECT_NORMAL,
                     candis_dock_probe_modules(&identity, &scan), ESP_OK,
                     "probe 0x51 and 0x52");
    api_test_emit("API_DOCK_MODULES|address_51=%s|address_52=%s\n",
                  scan.address_51 ? "ACK" : "NACK",
                  scan.address_52 ? "ACK" : "NACK");
    api_test_require(DAPI, "candis_dock_detect_start", API_ASPECT_BOUNDARY,
                     candis_dock_detect_start(NULL), ESP_ERR_INVALID_ARG, "NULL identity");
    api_test_require(DAPI, "candis_dock_detect_start", API_ASPECT_NORMAL,
                     candis_dock_detect_start(&identity), ESP_OK, "claim GPIO39 card detect");
    bool present = false;
    api_test_require(CAPI, "candis_storage_detect_read", API_ASPECT_NORMAL,
                     candis_storage_detect_read(&present), ESP_OK, "read Dock TF switch");
    api_test_require(CAPI, "candis_storage_detect_stop", API_ASPECT_NORMAL,
                     candis_storage_detect_stop(), ESP_OK, "release Dock TF switch");
    if (present) {
        mount_attempt(&identity);
    } else {
        api_test_log(DAPI, "candis_dock_mount", API_ASPECT_ERROR, API_PASS,
                     "card detect reports no card, mount skipped by contract");
        API_BLOCKED(DAPI, "candis_dock_mount", API_ASPECT_HARDWARE, "TF card is absent");
        API_BLOCKED(DAPI, "candis_dock_mount_with_power", API_ASPECT_HARDWARE, "TF card is absent");
        API_BLOCKED(DAPI, "candis_dock_mount_with_config", API_ASPECT_HARDWARE, "TF card is absent");
        api_test_require(DAPI, "candis_dock_mount_with_config", API_ASPECT_BOUNDARY,
                         candis_dock_mount_with_config(&identity, "/sdcard", NULL, NULL),
                         ESP_ERR_INVALID_ARG, "NULL mount config");
    }
    api_test_require(DAPI, "candis_dock_mount", API_ASPECT_BOUNDARY,
                     candis_dock_mount(NULL, "/sdcard", NULL), ESP_ERR_INVALID_ARG,
                     "NULL identity");
    api_test_require(DAPI, "candis_dock_mount_with_power", API_ASPECT_BOUNDARY,
                     candis_dock_mount_with_power(NULL, "/sdcard", false, NULL),
                     ESP_ERR_INVALID_ARG, "NULL identity");
    api_test_require(DAPI, "candis_dock_mount_with_config", API_ASPECT_BOUNDARY,
                     candis_dock_mount_with_config(NULL, "/sdcard", NULL, NULL),
                     ESP_ERR_INVALID_ARG, "NULL identity");
    candis_baseboard_info_t invalid = identity;
    invalid.hardware_revision = 0x0100;
    api_test_require(DAPI, "candis_dock_mount", API_ASPECT_ERROR,
                     candis_dock_mount(&invalid, "/sdcard", NULL), ESP_ERR_NOT_SUPPORTED,
                     "unsupported revision rejected");
    api_test_require(DAPI, "candis_dock_detect_start", API_ASPECT_ERROR,
                     candis_dock_detect_start(&invalid), ESP_ERR_NOT_SUPPORTED,
                     "unsupported revision cannot claim detect");
    api_test_require(DAPI, "candis_dock_probe_modules", API_ASPECT_ERROR,
                     candis_dock_probe_modules(&invalid, &scan), ESP_ERR_NOT_SUPPORTED,
                     "unsupported revision cannot probe modules");

    api_test_require(DAPI, "candis_dock_close", API_ASPECT_NORMAL,
                     candis_dock_close(), ESP_OK, "release bus, storage config and DC1SW");
    api_test_require(DAPI, "candis_dock_close", API_ASPECT_LIFECYCLE,
                     candis_dock_close(), ESP_OK, "repeated close");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_LIFECYCLE,
                     candis_dock_read_eeprom(eeprom, 1), ESP_ERR_INVALID_STATE,
                     "EEPROM read invalid after close");
    api_test_require(DAPI, "candis_dock_probe_modules", API_ASPECT_LIFECYCLE,
                     candis_dock_probe_modules(&identity, &scan), ESP_ERR_INVALID_STATE,
                     "module probe invalid after close");
    api_test_require(DAPI, "candis_dock_detect_start", API_ASPECT_LIFECYCLE,
                     candis_dock_detect_start(&identity), ESP_ERR_INVALID_STATE,
                     "detect start invalid after close");
    api_test_require(DAPI, "candis_dock_open", API_ASPECT_LIFECYCLE,
                     candis_dock_open(false, &unpowered), ESP_OK,
                     "reopen after complete close");
    api_test_require(DAPI, "candis_dock_read_eeprom", API_ASPECT_LIFECYCLE,
                     candis_dock_read_eeprom(eeprom, 1), ESP_OK,
                     "EEPROM readable after reopen");
    api_test_require(DAPI, "candis_dock_close", API_ASPECT_LIFECYCLE,
                     candis_dock_close(), ESP_OK, "close reopened session");
    api_test_require(DAPI, "candis_expansion_power_unclaim", API_ASPECT_LIFECYCLE,
                     candis_expansion_power_unclaim(&fixture_power), ESP_OK,
                     "release Dock fixture DC1SW reference");
    api_test_suite_end(DAPI);
}
