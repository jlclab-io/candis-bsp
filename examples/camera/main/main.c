/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "bsp/candis_ov5640.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_video_ioctl.h"
#include "linux/videodev2.h"

#define FRAME_COUNT 32
#define BUFFER_COUNT 2
static const char *TAG = "candis_ov5640_demo";
static const enum v4l2_buf_type CAPTURE = V4L2_BUF_TYPE_VIDEO_CAPTURE;

typedef struct {
    void *address;
    size_t length;
} buffer_t;

static esp_err_t setup_capture(int fd, buffer_t buffers[BUFFER_COUNT], size_t frame_bytes)
{
    struct timeval timeout = {.tv_sec = 2};
    ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_S_DQBUF_TIMEOUT, &timeout) == 0, ESP_FAIL, TAG, "set dequeue timeout");
    struct v4l2_requestbuffers request = {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP, .count = BUFFER_COUNT};
    ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_REQBUFS, &request) == 0 && request.count == BUFFER_COUNT,
                        ESP_ERR_NO_MEM, TAG, "allocate frame buffers");
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        struct v4l2_buffer buffer = {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_QUERYBUF, &buffer) == 0 && buffer.length >= frame_bytes,
                            ESP_FAIL, TAG, "query frame buffer");
        buffers[i].address = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
        ESP_RETURN_ON_FALSE(buffers[i].address != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "map frame buffer");
        buffers[i].length = buffer.length;
        ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_QBUF, &buffer) == 0, ESP_FAIL, TAG, "queue frame buffer");
    }
    return ESP_OK;
}

static esp_err_t read_frames(int fd, const buffer_t buffers[BUFFER_COUNT], size_t bytes)
{
    const int64_t started = esp_timer_get_time();
    uint32_t first_hash = 0;
    unsigned changed = 0;
    for (unsigned n = 0; n < FRAME_COUNT; ++n) {
        struct v4l2_buffer frame = {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP};
        ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_DQBUF, &frame) == 0, ESP_ERR_TIMEOUT, TAG, "dequeue frame errno=%d", errno);
        ESP_RETURN_ON_FALSE(frame.index < BUFFER_COUNT && frame.bytesused >= bytes &&
                            frame.bytesused <= buffers[frame.index].length,
                            ESP_ERR_INVALID_SIZE, TAG, "unexpected frame size");
        const uint8_t *pixels = buffers[frame.index].address;
        uint32_t hash = 2166136261U;
        for (size_t i = 0; i < bytes; i += 257) {
            hash = (hash ^ pixels[i]) * 16777619U;
        }
        if (n == 0) {
            first_hash = hash;
        } else if (hash != first_hash) {
            ++changed;
        }
        ESP_RETURN_ON_FALSE(ioctl(fd, VIDIOC_QBUF, &frame) == 0, ESP_FAIL, TAG, "return frame");
    }
    ESP_LOGI(TAG, "Frames=%d sample_changes=%u elapsed_ms=%" PRId64,
             FRAME_COUNT, changed, (esp_timer_get_time() - started) / 1000);
    return ESP_OK;
}

static esp_err_t capture(void)
{
    int fd = open(candis_ov5640_device(), O_RDWR);
    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_NOT_FOUND, TAG, "open DVP device");
    esp_err_t err = ESP_FAIL;
    bool streaming = false;
    buffer_t buffers[BUFFER_COUNT] = {0};
    struct v4l2_format format = {.type = CAPTURE};
    if (ioctl(fd, VIDIOC_G_FMT, &format) == 0) {
        const struct v4l2_pix_format *pix = &format.fmt.pix;
        const uint32_t row_bytes = pix->width * 2;
        const uint32_t stride = pix->bytesperline ? pix->bytesperline : row_bytes;
        const size_t frame_bytes = (size_t)stride * pix->height;
        ESP_LOGI(TAG, "DVP %" PRIu32 "x%" PRIu32 " fourcc=0x%08" PRIx32 " stride=%" PRIu32 " bytes=%u",
                 pix->width, pix->height, pix->pixelformat, stride, (unsigned)frame_bytes);
        if ((pix->pixelformat == V4L2_PIX_FMT_RGB565 || pix->pixelformat == V4L2_PIX_FMT_RGB565X) &&
            pix->width && pix->height && stride >= row_bytes) {
            err = setup_capture(fd, buffers, frame_bytes);
        }
        if (err == ESP_OK) {
            enum v4l2_buf_type type = CAPTURE;
            streaming = ioctl(fd, VIDIOC_STREAMON, &type) == 0;
            err = streaming ? read_frames(fd, buffers, frame_bytes) : ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "query capture format: errno=%d", errno);
    }
    if (streaming) {
        enum v4l2_buf_type type = CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMOFF, &type) && err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        if (buffers[i].length) {
            munmap(buffers[i].address, buffers[i].length);
        }
    }
    if (close(fd) != 0 && err == ESP_OK) {
        err = ESP_FAIL;
    }
    return err;
}

void app_main(void)
{
    for (unsigned cycle = 1; cycle <= 2; ++cycle) {
        esp_err_t err = candis_ov5640_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera start: %s", esp_err_to_name(err));
            return;
        }
        err = capture();
        esp_err_t stopped = candis_ov5640_stop();
        ESP_LOGI(TAG, "cycle %u capture=%s stop=%s", cycle, esp_err_to_name(err), esp_err_to_name(stopped));
        if (err != ESP_OK || stopped != ESP_OK) {
            return;
        }
    }
}
