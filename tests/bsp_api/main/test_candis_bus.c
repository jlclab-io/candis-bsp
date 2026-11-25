/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "api_test.h"
#include "bsp/candis.h"
#include "bsp/candis_baseboard.h"
#include "bsp/candis_baseboard_bus.h"
#include "bsp/candis_power.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#define CAPI "candis"

static uint16_t crc16_modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xa001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void seal_descriptor(uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE])
{
    static const struct {
        size_t begin;
        size_t end;
    } sections[] = {{0, 0x34}, {0x36, 0x3e}, {0x40, 0x84}};
    for (size_t i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        const uint16_t crc = crc16_modbus(image + sections[i].begin,
                                          sections[i].end - sections[i].begin);
        image[sections[i].end] = (uint8_t)crc;
        image[sections[i].end + 1] = (uint8_t)(crc >> 8);
    }
}

static void make_descriptor(uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE], uint16_t revision)
{
    memset(image, 0, CANDIS_BASEBOARD_IMAGE_SIZE);
    image[0] = 'E';
    image[1] = 'S';
    image[2] = 'P';
    image[3] = 0x31;
    image[4] = CANDIS_BASEBOARD_BB;
    image[5] = 0;
    image[6] = (uint8_t)revision;
    image[7] = (uint8_t)(revision >> 8);
    image[8] = 0;
    image[9] = 1;
    image[10] = 0x4e;
    image[11] = 0xca;
    memset(image + 0x14, '!', 32);
    image[0x40] = 1;
    seal_descriptor(image);
}

static void test_i2c(void)
{
    i2c_master_bus_handle_t first = NULL;
    i2c_master_bus_handle_t second = NULL;
    api_test_require(CAPI, "bsp_i2c_init", API_ASPECT_NORMAL, bsp_i2c_init(), ESP_OK,
                     "first acquisition");
    first = bsp_i2c_get_handle();
    api_test_check_bool(CAPI, "bsp_i2c_get_handle", API_ASPECT_NORMAL, first != NULL,
                        "borrowed handle after init");
    api_test_require(CAPI, "bsp_i2c_init", API_ASPECT_LIFECYCLE, bsp_i2c_init(), ESP_OK,
                     "repeated acquisition is idempotent");
    api_test_require(CAPI, "bsp_i2c_deinit", API_ASPECT_LIFECYCLE, bsp_i2c_deinit(), ESP_OK,
                     "release BSP reference");
    api_test_require(CAPI, "bsp_i2c_deinit", API_ASPECT_LIFECYCLE, bsp_i2c_deinit(), ESP_OK,
                     "repeated release is safe");
    api_test_require(CAPI, "candis_i2c_acquire", API_ASPECT_BOUNDARY,
                     candis_i2c_acquire(NULL), ESP_ERR_INVALID_ARG, "NULL output");
    api_test_require(CAPI, "candis_i2c_release", API_ASPECT_BOUNDARY,
                     candis_i2c_release(NULL), ESP_ERR_INVALID_ARG, "NULL handle");
    api_test_require(CAPI, "candis_i2c_acquire", API_ASPECT_NORMAL,
                     candis_i2c_acquire(&second), ESP_OK, "private acquisition");
    api_test_check_bool(CAPI, "candis_i2c_acquire", API_ASPECT_NORMAL, second != NULL,
                        "private handle is non-NULL");
    api_test_require(CAPI, "candis_i2c_release", API_ASPECT_NORMAL,
                     candis_i2c_release(second), ESP_OK, "private release");
    api_test_require(CAPI, "candis_i2c_release", API_ASPECT_ERROR,
                     candis_i2c_release(second), ESP_ERR_INVALID_STATE,
                     "stale handle rejected after release");
    (void)bsp_i2c_init();
}

static void test_baseboard_decode(void)
{
    uint8_t image[CANDIS_BASEBOARD_IMAGE_SIZE];
    candis_baseboard_info_t info = {0};
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_BOUNDARY,
                     candis_baseboard_decode(NULL, sizeof(image), &info),
                     ESP_ERR_INVALID_ARG, "NULL image");
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_BOUNDARY,
                     candis_baseboard_decode(image, sizeof(image), NULL),
                     ESP_ERR_INVALID_ARG, "NULL output");
    make_descriptor(image, 0x0102);
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_NORMAL,
                     candis_baseboard_decode(image, sizeof(image), &info), ESP_OK,
                     "synthetic Dock v1.2 descriptor");
    api_test_check_bool(CAPI, "candis_baseboard_decode", API_ASPECT_NORMAL,
                        info.kind == CANDIS_BASEBOARD_BB && info.hardware_revision == 0x0102 &&
                        info.format_revision == 1 && strcmp(info.name, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!") == 0,
                        "decoded identity and name");
    image[0x20] ^= 1;
    memset(&info, 0x55, sizeof(info));
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_ERROR,
                     candis_baseboard_decode(image, sizeof(image), &info), ESP_ERR_INVALID_CRC,
                     "corrupt first section");
    api_test_require(CAPI, "candis_baseboard_decode", API_ASPECT_BOUNDARY,
                     candis_baseboard_decode(image, sizeof(image) - 1, &info),
                     ESP_ERR_INVALID_SIZE, "short descriptor");
}

static void test_formal_dock_baseboard_probe(void)
{
    candis_expansion_power_handle_t power = NULL;
    i2c_master_bus_handle_t expansion = NULL;
    candis_baseboard_info_t info = {0};
    esp_err_t err = candis_expansion_power_claim(true, &power);
    if (err != ESP_OK) {
        API_BLOCKED(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_HARDWARE,
                    "formal Dock DC1SW is unavailable");
        return;
    }

    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_1,
        .scl_io_num = GPIO_NUM_58,
        .sda_io_num = GPIO_NUM_59,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    err = i2c_new_master_bus(&config, &expansion);
    if (err == ESP_OK) {
        err = candis_baseboard_probe_on_bus(expansion, 0x50, &info);
        if (err == ESP_OK) {
            api_test_require(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_HARDWARE,
                             err, ESP_OK, "legacy Dock description decoded on expansion bus");
            api_test_check_bool(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_HARDWARE,
                                info.kind == CANDIS_BASEBOARD_BB &&
                                (info.hardware_revision == 0x0101 || info.hardware_revision == 0x0102),
                                "formal Dock descriptor identity is supported");
        } else {
            API_BLOCKED(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_HARDWARE,
                        "formal Dock EEPROM not accessible on expansion bus");
        }
        const esp_err_t cleanup = i2c_del_master_bus(expansion);
        api_test_require(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_LIFECYCLE,
                         cleanup, ESP_OK, "release caller-owned expansion bus");
    } else {
        API_BLOCKED(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_HARDWARE,
                    "temporary Dock expansion bus unavailable");
    }
    api_test_require(CAPI, "candis_expansion_power_unclaim", API_ASPECT_LIFECYCLE,
                     candis_expansion_power_unclaim(&power), ESP_OK,
                     "release temporary Dock DC1SW reference");
}

static void test_baseboard_bus(i2c_master_bus_handle_t bus)
{
    candis_baseboard_info_t info;
    candis_capability_state_t state = CANDIS_CAPABILITY_UNSUPPORTED;
    uint32_t responding = 0;
    const uint8_t addresses[] = {0x34, 0x50, 0x68};

    api_test_require(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_BOUNDARY,
                     candis_baseboard_probe_on_bus(NULL, 0x50, &info),
                     ESP_ERR_INVALID_ARG, "NULL bus");
    api_test_require(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_BOUNDARY,
                     candis_baseboard_probe_on_bus(bus, 0x07, &info),
                     ESP_ERR_INVALID_ARG, "reserved I2C address");
    api_test_require(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_BOUNDARY,
                     candis_baseboard_probe_on_bus(bus, 0x50, NULL),
                     ESP_ERR_INVALID_ARG, "NULL output");
    api_test_require(CAPI, "candis_baseboard_probe_state_on_bus", API_ASPECT_BOUNDARY,
                     candis_baseboard_probe_state_on_bus(CANDIS_BASEBOARD_BB, bus, 0x50, NULL, &info),
                     ESP_ERR_INVALID_ARG, "NULL state");
    api_test_require(CAPI, "candis_baseboard_probe_state_on_bus", API_ASPECT_BOUNDARY,
                     candis_baseboard_probe_state_on_bus(CANDIS_BASEBOARD_BB, NULL, 0x50,
                                                         &state, &info),
                     ESP_ERR_INVALID_ARG, "NULL bus");
    const esp_err_t probe = candis_baseboard_probe_on_bus(bus, 0x50, &info);
    if (probe == ESP_OK) {
        api_test_log(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_NORMAL, API_PASS,
                     "EEPROM descriptor decoded from caller bus");
    } else {
        api_test_check_bool(CAPI, "candis_baseboard_probe_on_bus", API_ASPECT_ERROR,
                            probe != ESP_OK, "descriptor not present on core I2C bus");
    }
    api_test_require(CAPI, "candis_baseboard_probe_state_on_bus", API_ASPECT_NORMAL,
                     candis_baseboard_probe_state_on_bus((candis_baseboard_kind_t)0x30, bus, 0x50,
                                                         &state, NULL), ESP_OK,
                     "unsupported kind is a successful classification");
    api_test_check_bool(CAPI, "candis_baseboard_probe_state_on_bus", API_ASPECT_NORMAL,
                        state == CANDIS_CAPABILITY_UNSUPPORTED, "classified UNSUPPORTED");
    api_test_require(CAPI, "candis_i2c_probe_addresses", API_ASPECT_BOUNDARY,
                     candis_i2c_probe_addresses(NULL, addresses, 3, &responding),
                     ESP_ERR_INVALID_ARG, "NULL bus");
    api_test_require(CAPI, "candis_i2c_probe_addresses", API_ASPECT_BOUNDARY,
                     candis_i2c_probe_addresses(bus, NULL, 3, &responding),
                     ESP_ERR_INVALID_ARG, "NULL addresses");
    api_test_require(CAPI, "candis_i2c_probe_addresses", API_ASPECT_BOUNDARY,
                     candis_i2c_probe_addresses(bus, addresses, 0, &responding),
                     ESP_ERR_INVALID_ARG, "zero count");
    api_test_require(CAPI, "candis_i2c_probe_addresses", API_ASPECT_BOUNDARY,
                     candis_i2c_probe_addresses(bus, addresses, 33, &responding),
                     ESP_ERR_INVALID_ARG, "count above 32");
    api_test_require(CAPI, "candis_i2c_probe_addresses", API_ASPECT_NORMAL,
                     candis_i2c_probe_addresses(bus, addresses, 3, &responding), ESP_OK,
                     "probe core bus addresses");
    api_test_emit("API_DEVICE_SCAN|bus=core|version=%s|responding=0x%08" PRIx32
                  "|addresses=0x34,0x50,0x68\n", api_test_idf(), responding);
}

void api_tests_candis_bus(void)
{
    test_i2c();
    test_baseboard_decode();
    test_formal_dock_baseboard_probe();
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus) {
        test_baseboard_bus(bus);
    } else {
        API_BLOCKED(CAPI, "candis_baseboard_bus", API_ASPECT_HARDWARE,
                    "core I2C bus unavailable");
    }
}
