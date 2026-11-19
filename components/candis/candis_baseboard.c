/* SPDX-License-Identifier: Apache-2.0 */
/* Candis, Gamepad and BB use the ESP-Mosaico V1 wire layout. */
#include <stdbool.h>
#include <string.h>
#include "bsp/candis_baseboard.h"

static uint16_t le16(const uint8_t *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}

static bool crc_matches(const uint8_t *begin, size_t bytes)
{
    uint16_t crc = 0xffff;
    for (size_t n = 0; n < bytes; ++n) {
        crc ^= begin[n];
        for (int bit = 0; bit < 8; ++bit) {
            const uint16_t feedback = (crc & 1) ? 0xa001 : 0;
            crc = (crc >> 1) ^ feedback;
        }
    }
    return crc == le16(begin + bytes);
}

esp_err_t candis_baseboard_decode(const uint8_t *image, size_t length, candis_baseboard_info_t *info)
{
    if (!image || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length != CANDIS_BASEBOARD_IMAGE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (memcmp(image, "ESP", 3) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!crc_matches(image, 0x34) || !crc_matches(image + 0x36, 8) || !crc_matches(image + 0x40, 0x44)) {
        return ESP_ERR_INVALID_CRC;
    }
    if (le16(image + 0x42) > 64) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (le16(image + 0x0a) != 0xca4e || le16(image + 8) != 0x0100 || le16(image + 0x40) != 1 ||
        (image[0x0c] & ~1U) || image[0x0d] || image[0x0e] || image[0x0f]) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint16_t board = le16(image + 4), revision = le16(image + 6);
    candis_baseboard_kind_t kind;
    if (image[3] == 0x30 && board == 1 && revision == 0x0100) {
        kind = CANDIS_BASEBOARD_GAMEPAD;
    } else if (image[3] == 0x31 && board == 2 && revision == 0x0102) {
        kind = CANDIS_BASEBOARD_BB;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    candis_baseboard_info_t result = {.kind = kind, .hardware_revision = revision, .format_revision = 1};
    memcpy(result.name, image + 0x14, sizeof(result.name) - 1);
    *info = result;
    return ESP_OK;
}
