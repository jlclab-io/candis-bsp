/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "bsp/candis_camera.h"
#include "candis_camera_profile_internal.h"
#include "candis_camera_sensor.h"
#include "esp_check.h"
#include "esp_video_ioctl.h"
#include "linux/v4l2-controls.h"

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

typedef struct {
    candis_camera_control_id_t id;
    uint32_t v4l2_id;
    uint32_t control_class;
} control_map_t;

static const control_map_t s_control_map[] = {
    {CANDIS_CAMERA_CONTROL_GAIN, V4L2_CID_GAIN, V4L2_CTRL_CLASS_USER},
    {CANDIS_CAMERA_CONTROL_EXPOSURE, V4L2_CID_EXPOSURE, V4L2_CTRL_CLASS_USER},
    {CANDIS_CAMERA_CONTROL_HFLIP, V4L2_CID_HFLIP, V4L2_CTRL_CLASS_USER},
    {CANDIS_CAMERA_CONTROL_VFLIP, V4L2_CID_VFLIP, V4L2_CTRL_CLASS_USER},
    {CANDIS_CAMERA_CONTROL_AE_LEVEL, V4L2_CID_CAMERA_AE_LEVEL, V4L2_CTRL_CLASS_CAMERA},
    {CANDIS_CAMERA_CONTROL_EXPOSURE_100US, V4L2_CID_EXPOSURE_ABSOLUTE, V4L2_CTRL_CLASS_CAMERA},
    {CANDIS_CAMERA_CONTROL_JPEG_QUALITY, V4L2_CID_JPEG_COMPRESSION_QUALITY, V4L2_CTRL_CLASS_JPEG},
    {CANDIS_CAMERA_CONTROL_TEST_PATTERN, V4L2_CID_TEST_PATTERN, V4L2_CTRL_CLASS_IMAGE_PROC},
};

static const control_map_t *find_control(candis_camera_control_id_t id)
{
    for (size_t i = 0; i < sizeof(s_control_map) / sizeof(s_control_map[0]); ++i) {
        if (s_control_map[i].id == id) {
            return &s_control_map[i];
        }
    }
    return NULL;
}

static esp_err_t video_ioctl(unsigned long command, void *arg, const char *operation)
{
    if (ioctl(s_fd, command, arg) == 0) {
        return ESP_OK;
    }
    const int error = errno;
    if ((command == VIDIOC_ENUM_FMT || command == VIDIOC_ENUM_SENSOR_FMT) && error == EINVAL) {
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
        /* ESP Video applies the configured sensor mode when opening the device. */
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
    if (s_fd < 0 || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    struct v4l2_ext_controls controls = {
        .ctrl_class = control_class,
        .count = 1,
        .controls = control,
    };
    return video_ioctl(request, &controls, operation);
}

esp_err_t candis_camera_control_query(candis_camera_control_id_t id,
                                      candis_camera_control_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    const control_map_t *map = find_control(id);
    if (!map) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_borrowed || (s_fd >= 0 && !s_streaming)) {
        return ESP_ERR_INVALID_STATE;
    }
    const candis_camera_profile_t *profile;
    ESP_RETURN_ON_ERROR(candis_camera_get_active_profile(&profile), TAG, "identify camera for control");
    ESP_RETURN_ON_FALSE(candis_camera_profile_supports_control(profile, id), ESP_ERR_NOT_SUPPORTED, TAG,
                        "camera profile does not advertise this control");
    if (id == CANDIS_CAMERA_CONTROL_TEST_PATTERN) {
        *info = (candis_camera_control_info_t) {
            .id = id,
            .v4l2_id = map->v4l2_id,
            .type = V4L2_CTRL_TYPE_BOOLEAN,
            .flags = V4L2_CTRL_FLAG_WRITE_ONLY,
            .minimum = 0,
            .maximum = 1,
            .step = 1,
            .default_value = 0,
        };
        memcpy(info->name, "Test Pattern", sizeof("Test Pattern"));
        return ESP_OK;
    }
    const bool opened_here = s_fd < 0;
    if (opened_here) {
        ESP_RETURN_ON_ERROR(open_device(), TAG, "open camera for control query");
    }
    struct v4l2_query_ext_ctrl query = {.id = map->v4l2_id};
    const esp_err_t err = video_ioctl(VIDIOC_QUERY_EXT_CTRL, &query, "query camera control");
    const esp_err_t cleanup = opened_here ? candis_camera_stream_stop() : ESP_OK;
    if (cleanup != ESP_OK) {
        return cleanup;
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (err != ESP_OK) {
        return err;
    }
    *info = (candis_camera_control_info_t) {
        .id = id,
        .v4l2_id = map->v4l2_id,
        .type = query.type,
        .flags = query.flags,
        .minimum = query.minimum,
        .maximum = query.maximum,
        .step = query.step,
        .default_value = query.default_value,
    };
    memcpy(info->name, query.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1] = 0;
    if (!candis_camera_profile_can_read_control(profile, id)) {
        info->flags |= V4L2_CTRL_FLAG_WRITE_ONLY;
    }
    return ESP_OK;
}

esp_err_t candis_camera_control_get(candis_camera_control_id_t id, int32_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    const control_map_t *map = find_control(id);
    if (!map) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_streaming || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    const candis_camera_profile_t *profile;
    ESP_RETURN_ON_ERROR(candis_camera_get_active_profile(&profile), TAG, "identify camera for control");
    if (!candis_camera_profile_can_read_control(profile, id)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    struct v4l2_ext_control control = {.id = map->v4l2_id};
    const esp_err_t err = sensor_control(VIDIOC_G_EXT_CTRLS, map->control_class,
                                         &control, "get camera control");
    if (err == ESP_OK) {
        *value = control.value;
    }
    return err;
}

esp_err_t candis_camera_control_set(candis_camera_control_id_t id, int32_t value)
{
    const control_map_t *map = find_control(id);
    if (!map) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_streaming || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
    const candis_camera_profile_t *profile;
    ESP_RETURN_ON_ERROR(candis_camera_get_active_profile(&profile), TAG, "identify camera for control");
    ESP_RETURN_ON_FALSE(candis_camera_profile_supports_control(profile, id), ESP_ERR_NOT_SUPPORTED, TAG,
                        "camera profile does not advertise this control");
    if (id == CANDIS_CAMERA_CONTROL_TEST_PATTERN && (value < 0 || value > 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    struct v4l2_ext_control control = {.id = map->v4l2_id, .value = value};
    return sensor_control(VIDIOC_S_EXT_CTRLS, map->control_class,
                          &control, "set camera control");
}

esp_err_t candis_camera_read_sensor_id(esp_cam_sensor_id_t *id)
{
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool opened_here = s_fd < 0;
    if (opened_here) {
        ESP_RETURN_ON_ERROR(open_device(), TAG, "open camera for sensor ID");
    }
    esp_cam_sensor_id_t value = {0};
    struct v4l2_ext_control control = {
        .id = ESP_CAM_SENSOR_IOC_G_CHIP_ID, .p_u8 = (uint8_t *)&value, .size = sizeof(value),
    };
    esp_err_t err = sensor_control(VIDIOC_G_EXT_CTRLS, V4L2_CTRL_CLASS_ESP_CAM_IOCTL, &control, "read sensor ID");
    const esp_err_t cleanup = opened_here ? candis_camera_stream_stop() : ESP_OK;
    if (err == ESP_OK && cleanup == ESP_OK) {
        *id = value;
    }
    return err == ESP_OK ? cleanup : err;
}

esp_err_t candis_camera_set_flip(bool horizontal, bool enabled)
{
    return candis_camera_control_set(horizontal ? CANDIS_CAMERA_CONTROL_HFLIP :
                                     CANDIS_CAMERA_CONTROL_VFLIP, enabled);
}

esp_err_t candis_camera_set_test_pattern(bool enabled)
{
    return candis_camera_control_set(CANDIS_CAMERA_CONTROL_TEST_PATTERN, enabled);
}

esp_err_t candis_camera_frame_acquire(candis_camera_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_streaming || s_borrowed) {
        return ESP_ERR_INVALID_STATE;
    }
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
        const esp_err_t stop = video_ioctl(VIDIOC_STREAMOFF, &type, "stop capture");
        if (stop != ESP_OK) {
            return stop;
        }
        s_streaming = false;
        s_borrowed = false;
    }
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        if (s_buffers[i].length) {
            if (munmap(s_buffers[i].address, s_buffers[i].length) == 0) {
                s_buffers[i].length = 0;
            } else {
                ESP_LOGE(TAG, "unmap frame buffer %u: errno=%d", i, errno);
            }
        }
    }
    if (s_buffers[0].length || s_buffers[1].length) {
        return ESP_FAIL;
    }
    const int fd = s_fd;
    s_fd = -1;
    if (close(fd) != 0) {
        ESP_LOGE(TAG, "close DVP: errno=%d", errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool candis_camera_stream_is_open(void)
{
    return s_fd >= 0;
}
