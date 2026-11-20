/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bsp/candis_storage.h"
#include "esp_check.h"
#include "tf_verify.h"

enum { BLOCK_SIZE = 256, BLOCKS = 16, NAMES = 32 };
static const char *TAG = "candis_tf_verify";

static void fill(uint8_t block[BLOCK_SIZE], unsigned index)
{
    for (unsigned i = 0; i < BLOCK_SIZE; ++i) {
        block[i] = (uint8_t)(i * 37 + index * 17);
    }
}

static esp_err_t require_card(void)
{
    bool present;
    const esp_err_t err = candis_storage_detect_read(&present);
    return err == ESP_OK && !present ? ESP_ERR_NOT_FOUND : err;
}

static esp_err_t write_blocks(int fd)
{
    uint8_t block[BLOCK_SIZE];
    for (unsigned i = 0; i < BLOCKS; ++i) {
        ESP_RETURN_ON_ERROR(require_card(), TAG, "card lost during write");
        fill(block, i);
        ESP_RETURN_ON_FALSE(write(fd, block, sizeof(block)) == (ssize_t)sizeof(block),
                            ESP_FAIL, TAG, "write test block %u", i);
    }
    ESP_RETURN_ON_ERROR(require_card(), TAG, "card lost before sync");
    return fsync(fd) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t read_blocks(int fd)
{
    uint8_t expected[BLOCK_SIZE], actual[BLOCK_SIZE];
    for (unsigned i = 0; i < BLOCKS; ++i) {
        ESP_RETURN_ON_ERROR(require_card(), TAG, "card lost during read");
        ESP_RETURN_ON_FALSE(read(fd, actual, sizeof(actual)) == (ssize_t)sizeof(actual),
                            ESP_ERR_INVALID_SIZE, TAG, "read test block %u", i);
        fill(expected, i);
        ESP_RETURN_ON_FALSE(memcmp(expected, actual, sizeof(actual)) == 0,
                            ESP_ERR_INVALID_CRC, TAG, "compare test block %u", i);
    }
    return read(fd, actual, 1) == 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t verify_file(const char *path, int fd)
{
    esp_err_t err = write_blocks(fd);
    const int closed = close(fd);
    ESP_RETURN_ON_ERROR(err, TAG, "write and synchronize file");
    ESP_RETURN_ON_FALSE(closed == 0, ESP_FAIL, TAG, "close written file");
    ESP_RETURN_ON_ERROR(require_card(), TAG, "card lost before reopening");
    fd = open(path, O_RDONLY);
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_FAIL, TAG, "reopen test file");
    err = read_blocks(fd);
    const int read_closed = close(fd);
    return err == ESP_OK && read_closed != 0 ? ESP_FAIL : err;
}

esp_err_t candis_example_tf_verify(void)
{
    ESP_RETURN_ON_ERROR(require_card(), TAG, "card required for file test");
    char path[sizeof(CANDIS_EXAMPLE_TF_PATH "/CDS0000.TST")];
    int fd = -1;
    for (unsigned i = 0; i < NAMES; ++i) {
        snprintf(path, sizeof(path), CANDIS_EXAMPLE_TF_PATH "/CDS%04u.TST", i);
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            break;
        }
        ESP_RETURN_ON_FALSE(errno == EEXIST, ESP_FAIL, TAG, "create test file: errno=%d", errno);
    }
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_NOT_FOUND, TAG, "all test filenames in use");
    const esp_err_t verify = verify_file(path, fd);
    esp_err_t cleanup = require_card();
    if (cleanup == ESP_OK && unlink(path) != 0) {
        cleanup = ESP_FAIL;
    }
    ESP_LOGI(TAG, "TF file=%s expected_bytes=%d verify=%s cleanup=%s", path, BLOCKS * BLOCK_SIZE,
             esp_err_to_name(verify), esp_err_to_name(cleanup));
    return verify == ESP_OK ? cleanup : verify;
}
