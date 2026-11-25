/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bsp/candis_storage.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "camera_pixels.h"
#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "still_jpeg.h"

static const char *TAG = "candis_still";
#define MOUNT_PATH "/sdcard"

static esp_err_t verify_photo(const char *path, const uint8_t *data, uint32_t length)
{
    const int fd = open(path, O_RDONLY);
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_FAIL, TAG, "reopen %s: errno=%d", path, errno);

    uint8_t buffer[512];
    uint32_t offset = 0;
    while (offset < length) {
        const size_t chunk = length - offset < sizeof(buffer) ? length - offset : sizeof(buffer);
        const ssize_t count = read(fd, buffer, chunk);
        if (count <= 0 || memcmp(buffer, data + offset, (size_t)count) != 0) {
            break;
        }
        offset += (uint32_t)count;
    }
    const bool matches = offset == length && read(fd, buffer, 1) == 0;
    const int closed = close(fd);
    ESP_RETURN_ON_FALSE(matches && closed == 0, ESP_FAIL, TAG,
                        "verify %s: matched %" PRIu32 "/%" PRIu32 " bytes", path, offset, length);
    return ESP_OK;
}

static esp_err_t write_photo(const uint8_t *data, uint32_t length)
{
    char path[sizeof(MOUNT_PATH "/IMG_9999.JPG")];
    int fd = -1;
    for (unsigned index = 1; index <= 9999; ++index) {
        snprintf(path, sizeof(path), MOUNT_PATH "/IMG_%04u.JPG", index);
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            break;
        }
        ESP_RETURN_ON_FALSE(errno == EEXIST, ESP_FAIL, TAG, "create photo: errno=%d", errno);
    }
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_NOT_FOUND, TAG, "all photo names are in use");

    bool saved = write(fd, data, length) == (ssize_t)length && fsync(fd) == 0;
    if (close(fd) != 0) {
        saved = false;
    }
    if (!saved || verify_photo(path, data, length) != ESP_OK) {
        ESP_LOGE(TAG, "could not save and verify %s", path);
        if (unlink(path) != 0) {
            ESP_LOGE(TAG, "remove incomplete photo %s: errno=%d", path, errno);
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Saved and verified %s (%" PRIu32 " bytes)", path, length);
    return ESP_OK;
}

static void pack_rgb565(const struct v4l2_pix_format *format, uint8_t *raw, bool swap)
{
    const size_t row_bytes = format->width * sizeof(uint16_t);
    const bool yuyv = format->pixelformat == V4L2_PIX_FMT_YUYV;
    const bool yuv = yuyv || format->pixelformat == V4L2_PIX_FMT_UYVY;
    const bool limited = format->quantization == V4L2_QUANTIZATION_LIM_RANGE;
    for (unsigned y = 0; y < format->height; ++y) {
        uint8_t *row = raw + y * row_bytes;
        memmove(row, raw + (size_t)y * format->bytesperline, row_bytes);
        uint16_t *pixels = (uint16_t *)row;
        if (yuv) {
            for (unsigned x = 0; x < format->width; x += 2) {
                const uint8_t *pair = row + x * sizeof(uint16_t);
                const uint16_t first = camera_yuv_pixel(pair, yuyv, 0, limited);
                const uint16_t second = camera_yuv_pixel(pair, yuyv, 1, limited);
                pixels[x] = first;
                pixels[x + 1] = second;
            }
        } else if (swap) {
            /* Use the same DMA byte order as the validated RGB565 preview. */
            for (unsigned x = 0; x < format->width; ++x) {
                pixels[x] = __builtin_bswap16(pixels[x]);
            }
        }
    }
}

esp_err_t camera_save_still_jpeg(const struct v4l2_pix_format *format, uint8_t *raw, bool swap)
{
    const uint32_t raw_size = format->width * format->height * sizeof(uint16_t);
    pack_rgb565(format, raw, swap);
    const jpeg_encode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER};
    size_t capacity;
    uint8_t *jpeg = jpeg_alloc_encoder_mem(raw_size, &mem, &capacity);
    ESP_RETURN_ON_FALSE(jpeg, ESP_ERR_NO_MEM, TAG, "allocate JPEG output");

    const jpeg_encode_engine_cfg_t engine = {.timeout_ms = 500};
    jpeg_encoder_handle_t handle;
    esp_err_t err = jpeg_new_encoder_engine(&engine, &handle);
    if (err == ESP_OK) {
        const jpeg_encode_cfg_t cfg = {
            .width = format->width,
            .height = format->height,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
            .image_quality = 80,
        };
        uint32_t length;
        err = jpeg_encoder_process(handle, &cfg, raw, raw_size, jpeg, capacity, &length);
        const esp_err_t deleted = jpeg_del_encoder_engine(handle);
        if (err == ESP_OK) {
            err = deleted;
        }
        if (err == ESP_OK && (length < 4 || length > capacity || jpeg[0] != 0xff ||
                              jpeg[1] != 0xd8 || jpeg[length - 2] != 0xff || jpeg[length - 1] != 0xd9)) {
            ESP_LOGE(TAG, "invalid JPEG frame: encoded length=%" PRIu32, length);
            err = ESP_ERR_INVALID_RESPONSE;
        }
        if (err == ESP_OK) {
            candis_storage_config_t storage = CANDIS_STORAGE_CONFIG_DEFAULT();
            storage.slot.clk = GPIO_NUM_24;
            storage.slot.cmd = GPIO_NUM_25;
            storage.slot.d0 = GPIO_NUM_20;
            storage.slot.d1 = GPIO_NUM_21;
            storage.slot.d2 = GPIO_NUM_22;
            storage.slot.d3 = GPIO_NUM_23;
            storage.slot.cd = GPIO_NUM_35;
            storage.slot.wp = SDMMC_SLOT_NO_WP;
            storage.slot.width = 4;
            storage.slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
            storage.card_detect_active_low = true;
            storage.use_dc1sw = true;
            sdmmc_card_t *card;
            err = candis_storage_configure(&storage);
            if (err == ESP_OK) {
                err = candis_storage_mount(MOUNT_PATH, true, &card);
            }
            if (err == ESP_OK) {
                err = write_photo(jpeg, length);
            }
            const esp_err_t unmounted = candis_storage_unmount();
            ESP_ERROR_CHECK_WITHOUT_ABORT(unmounted);
            if (err == ESP_OK) {
                err = unmounted;
            }
            candis_storage_reset_config();
        }
    }
    free(jpeg);
    return err;
}
