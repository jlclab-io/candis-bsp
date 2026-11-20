/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "bsp/candis_camera.h"
#include "candis_camera_sensor.h"
#include "esp_check.h"
#include "esp_video_ioctl.h"

#define CAPTURE V4L2_BUF_TYPE_VIDEO_CAPTURE
#define BUFFER_COUNT 2

static const char *TAG = "candis_camera";
static int s_fd = -1;
static bool s_streaming;
static bool s_borrowed;
static struct v4l2_buffer s_frame;
static struct {
    void *address;
    size_t length;
} s_buffers[BUFFER_COUNT];

static esp_err_t video_ioctl(unsigned long command, void *arg, const char *operation)
{
    if (ioctl(s_fd, command, arg) == 0) {
        return ESP_OK;
    }
    const int error = errno;
    if (command == VIDIOC_ENUM_FMT && error == EINVAL) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGE(TAG, "%s: errno=%d", operation, error);
    switch (error) {
    case ETIMEDOUT: return ESP_ERR_TIMEOUT;
    case ENOMEM: return ESP_ERR_NO_MEM;
    case EBUSY: return ESP_ERR_INVALID_STATE;
    case EINVAL: return ESP_ERR_INVALID_ARG;
    case ENODEV: return ESP_ERR_NOT_FOUND;
    case ENOTTY:
    case ESRCH: return ESP_ERR_NOT_SUPPORTED;
    default: return ESP_FAIL;
    }
}

static esp_err_t open_device(void)
{
    const char *path = candis_camera_device();
    ESP_RETURN_ON_FALSE(path && s_fd < 0, ESP_ERR_INVALID_STATE, TAG, "camera not idle");
    s_fd = open(path, O_RDWR);
    ESP_RETURN_ON_FALSE(s_fd >= 0, ESP_FAIL, TAG, "open DVP errno=%d", errno);
    return ESP_OK;
}

esp_err_t candis_camera_get_mode(uint32_t index, esp_cam_sensor_format_t *mode)
{
    if (!mode) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(open_device(), TAG, "open camera for mode enumeration");
    struct v4l2_sensor_format_enum request = {.index = index};
    esp_err_t err = video_ioctl(VIDIOC_ENUM_SENSOR_FMT, &request, "enumerate sensor format");
    const esp_err_t cleanup = candis_camera_stream_stop();
    if (err == ESP_OK && cleanup == ESP_OK) {
        *mode = request.format;
    }
    return err == ESP_OK ? cleanup : err;
}

esp_err_t candis_camera_get_pixel_format(uint32_t index, struct v4l2_fmtdesc *format)
{
    if (!format) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(open_device(), TAG, "open camera for pixel format enumeration");
    struct v4l2_fmtdesc request = {.type = CAPTURE, .index = index};
    const esp_err_t err = video_ioctl(VIDIOC_ENUM_FMT, &request, "enumerate pixel format");
    const esp_err_t cleanup = candis_camera_stream_stop();
    if (cleanup != ESP_OK) {
        return cleanup;
    }
    if (err == ESP_OK) {
        *format = request;
    }
    return err;
}

static esp_err_t start_stream(const candis_camera_stream_config_t *config, struct v4l2_pix_format *format)
{
    if (config && config->mode) {
        esp_cam_sensor_format_t active;
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_G_SENSOR_FMT, &active, "get sensor mode"), TAG, "configure sensor");
        /* OV5640 in esp_cam_sensor 2.6.0 dereferences a NULL reset table for
         * explicit format changes. ESP Video applies the Kconfig mode on open. */
        const esp_cam_sensor_format_t *requested = config->mode;
        ESP_RETURN_ON_FALSE(requested->regs == active.regs && requested->format == active.format &&
                            requested->width == active.width && requested->height == active.height &&
                            requested->fps == active.fps, ESP_ERR_NOT_SUPPORTED, TAG,
                            "select the sensor mode in Kconfig before capture");
    }
    struct v4l2_format video_format = {.type = CAPTURE};
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_G_FMT, &video_format, "get capture format"), TAG, "configure capture");
    if (config && config->pixel_format && config->pixel_format != video_format.fmt.pix.pixelformat) {
        video_format.fmt.pix.pixelformat = config->pixel_format;
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_S_FMT, &video_format, "set capture format"), TAG, "configure capture");
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_G_FMT, &video_format, "get selected format"), TAG, "read capture format");
        if (video_format.fmt.pix.pixelformat != config->pixel_format) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    const uint32_t timeout_ms = config && config->timeout_ms ? config->timeout_ms : 2000;
    struct timeval timeout = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_S_DQBUF_TIMEOUT, &timeout, "set capture timeout"), TAG, "configure capture");
    struct v4l2_requestbuffers request = {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP, .count = BUFFER_COUNT};
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_REQBUFS, &request, "allocate frame buffers"), TAG, "initialize buffers");
    ESP_RETURN_ON_FALSE(request.count == BUFFER_COUNT, ESP_ERR_NO_MEM, TAG, "two capture buffers required");
    for (uint32_t i = 0; i < BUFFER_COUNT; ++i) {
        struct v4l2_buffer buffer = {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i};
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_QUERYBUF, &buffer, "query frame buffer"), TAG, "initialize buffer");
        void *address = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, buffer.m.offset);
        ESP_RETURN_ON_FALSE(address && address != MAP_FAILED, ESP_ERR_NO_MEM, TAG, "map frame buffer");
        s_buffers[i].address = address;
        s_buffers[i].length = buffer.length;
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_QBUF, &buffer, "queue frame buffer"), TAG, "initialize buffer");
    }
    enum v4l2_buf_type type = CAPTURE;
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_STREAMON, &type, "start capture"), TAG, "start DVP stream");
    s_streaming = true;
    if (format) {
        *format = video_format.fmt.pix;
    }
    return ESP_OK;
}

esp_err_t candis_camera_stream_start(const candis_camera_stream_config_t *config, struct v4l2_pix_format *format)
{
    ESP_RETURN_ON_ERROR(open_device(), TAG, "open camera for stream");
    const esp_err_t err = start_stream(config, format);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_stream_stop());
    }
    return err;
}

static esp_err_t sensor_control(unsigned long request, uint32_t control_class,
                                struct v4l2_ext_control *control, const char *operation)
{
    if (!s_streaming || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    struct v4l2_ext_controls controls = {
        .ctrl_class = control_class,
        .count = 1,
        .controls = control,
    };
    return video_ioctl(request, &controls, operation);
}

esp_err_t candis_camera_sensor_read_register(uint16_t address, uint8_t *value)
{
    esp_cam_sensor_reg_val_t reg = {.regaddr = address};
    struct v4l2_ext_control control = {
        .id = ESP_CAM_SENSOR_IOC_G_REG, .p_u8 = (uint8_t *)&reg, .size = sizeof(reg),
    };
    const esp_err_t err = sensor_control(VIDIOC_G_EXT_CTRLS, V4L2_CTRL_CLASS_ESP_CAM_IOCTL,
                                         &control, "read sensor register");
    if (err == ESP_OK) {
        *value = reg.value;
    }
    return err;
}

esp_err_t candis_camera_sensor_write_register(uint16_t address, uint8_t value)
{
    esp_cam_sensor_reg_val_t reg = {.regaddr = address, .value = value};
    struct v4l2_ext_control control = {
        .id = ESP_CAM_SENSOR_IOC_S_REG, .p_u8 = (uint8_t *)&reg, .size = sizeof(reg),
    };
    return sensor_control(VIDIOC_S_EXT_CTRLS, V4L2_CTRL_CLASS_ESP_CAM_IOCTL,
                          &control, "write sensor register");
}

esp_err_t candis_camera_read_sensor_id(esp_cam_sensor_id_t *id)
{
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_cam_sensor_id_t value = {0};
    struct v4l2_ext_control control = {
        .id = ESP_CAM_SENSOR_IOC_G_CHIP_ID, .p_u8 = (uint8_t *)&value, .size = sizeof(value),
    };
    esp_err_t err = sensor_control(VIDIOC_G_EXT_CTRLS, V4L2_CTRL_CLASS_ESP_CAM_IOCTL, &control, "read sensor ID");
    if (err == ESP_OK) {
        *id = value;
    }
    return err;
}

esp_err_t candis_camera_set_flip(bool horizontal, bool enabled)
{
    ESP_RETURN_ON_ERROR(candis_ov5640_end_pending_group(), TAG, "recover sensor group");
    struct v4l2_ext_control control = {
        .id = horizontal ? V4L2_CID_HFLIP : V4L2_CID_VFLIP, .value = enabled,
    };
    return sensor_control(VIDIOC_S_EXT_CTRLS, V4L2_CTRL_CLASS_USER, &control, "set sensor flip");
}

esp_err_t candis_camera_set_test_pattern(bool enabled)
{
    ESP_RETURN_ON_ERROR(candis_ov5640_end_pending_group(), TAG, "recover sensor group");
    int pattern = enabled;
    struct v4l2_ext_control control = {
        .id = ESP_CAM_SENSOR_IOC_S_TEST_PATTERN, .p_u8 = (uint8_t *)&pattern, .size = sizeof(pattern),
    };
    return sensor_control(VIDIOC_S_EXT_CTRLS, V4L2_CTRL_CLASS_ESP_CAM_IOCTL, &control, "set test pattern");
}

esp_err_t candis_camera_frame_acquire(candis_camera_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_streaming || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(candis_ov5640_end_pending_group(), TAG, "recover sensor group");
    s_frame = (struct v4l2_buffer) {.type = CAPTURE, .memory = V4L2_MEMORY_MMAP};
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_DQBUF, &s_frame, "dequeue frame"), TAG, "capture frame");
    s_borrowed = true;
    if (s_frame.index >= BUFFER_COUNT) {
        /* An invalid index cannot be sent back to QBUF. Stop to reclaim the buffers. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_stream_stop());
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (s_frame.bytesused > s_buffers[s_frame.index].length) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_frame_release());
        return ESP_ERR_INVALID_RESPONSE;
    }
    *frame = (candis_camera_frame_t) {
        .data = s_buffers[s_frame.index].address,
        .size = s_frame.bytesused,
        .sequence = s_frame.sequence,
        .flags = s_frame.flags,
    };
    return ESP_OK;
}

esp_err_t candis_camera_frame_release(void)
{
    if (!s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_QBUF, &s_frame, "return frame"), TAG, "release buffer");
    s_borrowed = false;
    return ESP_OK;
}

esp_err_t candis_camera_capture_frame(void *buffer, size_t capacity, size_t *size)
{
    if (!buffer || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    *size = 0;
    candis_camera_frame_t frame;
    esp_err_t err = candis_camera_frame_acquire(&frame);
    if (err != ESP_OK) {
        return err;
    }
    if ((frame.flags & V4L2_BUF_FLAG_ERROR) || !frame.size) {
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (frame.size > capacity) {
        err = ESP_ERR_INVALID_SIZE;
        *size = frame.size;
    } else {
        memcpy(buffer, frame.data, frame.size);
        *size = frame.size;
    }
    const esp_err_t release = candis_camera_frame_release();
    return release == ESP_OK ? err : release;
}

esp_err_t candis_camera_stream_stop(void)
{
    if (s_fd < 0) {
        return ESP_OK;
    }
    if (s_streaming) {
        enum v4l2_buf_type type = CAPTURE;
        ESP_RETURN_ON_ERROR(candis_ov5640_end_pending_group(), TAG, "recover sensor group before stopping");
        ESP_RETURN_ON_ERROR(video_ioctl(VIDIOC_STREAMOFF, &type, "stop capture"), TAG, "stop DVP stream");
        s_streaming = false;
        s_borrowed = false;
    }
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        if (s_buffers[i].length) {
            ESP_RETURN_ON_FALSE(munmap(s_buffers[i].address, s_buffers[i].length) == 0, ESP_FAIL, TAG, "unmap frame buffer");
            s_buffers[i].length = 0;
        }
    }
    const int fd = s_fd;
    s_fd = -1;
    ESP_RETURN_ON_FALSE(close(fd) == 0, ESP_FAIL, TAG, "close DVP errno=%d", errno);
    return ESP_OK;
}
