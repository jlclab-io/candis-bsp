/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "bsp/candis_camera_profile.h"
#include "esp_cam_sensor_types.h"
#include "esp_err.h"
#include "linux/videodev2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t data[8];
    gpio_num_t pclk;
    gpio_num_t vsync;
    gpio_num_t hsync;
    gpio_num_t xclk;                   /**< GPIO_NUM_NC for an externally supplied clock. */
    uint32_t xclk_hz;
    ledc_timer_t xclk_timer;
    ledc_channel_t xclk_channel;
    i2c_master_bus_handle_t sccb_bus;  /**< An optional borrowed handle. */
    i2c_port_num_t sccb_port;          /**< Use when ESP Video owns a dedicated bus. */
    gpio_num_t sccb_scl;               /**< GPIO_NUM_NC when using a borrowed bus. */
    gpio_num_t sccb_sda;               /**< GPIO_NUM_NC when using a borrowed bus. */
    uint32_t sccb_hz;
    /** Optional application resource callbacks. With SCCB pins set to
     * GPIO_NUM_NC, acquire may supply a shared SCCB handle.
     * Acquire runs before XCLK starts. Prepare runs after XCLK starts and
     * before sensor detection. Release runs after video and XCLK stop.
     * Release must handle partial acquisition and may be retried on error.
     * Keep context and borrowed resources valid until stop completes.
     */
    esp_err_t (*acquire)(void *context, i2c_master_bus_handle_t *sccb_bus);
    esp_err_t (*release)(void *context);
    void *context;
    bool validate_sensor;
    candis_camera_sensor_t expected_sensor;
    esp_err_t (*prepare)(void *context);
} candis_camera_config_t;

/** Start one configured DVP instance. Call lifecycle APIs from one control task.
 * Reserve the configured pins, XCLK LEDC resources and ESP Video DVP instance for
 * this component. Serialize ESP Video lifecycle calls in the application.
 * On a cleanup failure, call stop again before retrying start.
 */
esp_err_t candis_camera_init(const candis_camera_config_t *config);

/** Identify the active sensor after the DVP device has detected it. */
esp_err_t candis_camera_get_active_profile(const candis_camera_profile_t **profile);

typedef struct {
    candis_camera_control_id_t id;
    uint32_t v4l2_id;
    uint32_t type;
    uint32_t flags;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    int32_t default_value;
    char name[32];
} candis_camera_control_info_t;

/** Query a control after initialization, between borrowed frames. */
esp_err_t candis_camera_control_query(candis_camera_control_id_t id,
                                      candis_camera_control_info_t *info);
/** Read or write a control during capture, between borrowed frames.
 * Write-only sensor controls return ESP_ERR_NOT_SUPPORTED when read. */
esp_err_t candis_camera_control_get(candis_camera_control_id_t id, int32_t *value);
esp_err_t candis_camera_control_set(candis_camera_control_id_t id, int32_t value);

/** Native V4L2 path. Use either native capture or the stream API for a session. */
const char *candis_camera_device(void);

/** Enumerate compiled sensor modes starting at index zero, with capture stopped. */
esp_err_t candis_camera_get_mode(uint32_t index, esp_cam_sensor_format_t *mode);

/** Enumerate DVP output formats for the active sensor mode, with capture stopped.
 * Indexes start at zero. ESP_ERR_NOT_FOUND signals the end of the list.
 */
esp_err_t candis_camera_get_pixel_format(uint32_t index, struct v4l2_fmtdesc *format);

typedef struct {
    const esp_cam_sensor_format_t *mode; /**< Optional expected mode from get_mode(). The sensor selects its active mode in Kconfig. */
    uint32_t pixel_format;              /**< V4L2 fourcc. Zero keeps the current format. */
    uint32_t timeout_ms;                /**< Zero uses the default two-second timeout. */
} candis_camera_stream_config_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t sequence;
    uint32_t flags; /**< V4L2 flags. Check V4L2_BUF_FLAG_ERROR before using the payload. */
} candis_camera_frame_t;

/** Configure and start capture with two reusable DMA buffers. NULL config uses defaults.
 * The optional format output describes the actual dimensions, stride and pixel format.
 * Call all capture APIs from the same control task.
 */
esp_err_t candis_camera_stream_start(const candis_camera_stream_config_t *config, struct v4l2_pix_format *format);
/** Read the sensor identification after initialization, between borrowed frames. */
esp_err_t candis_camera_read_sensor_id(esp_cam_sensor_id_t *id);
/** Set one sensor orientation control between frames. */
esp_err_t candis_camera_set_flip(bool horizontal, bool enabled);
/** Toggle the sensor's diagnostic pattern between frames. */
esp_err_t candis_camera_set_test_pattern(bool enabled);
/** Borrow one frame until frame_release() or stream_stop(). Only one frame may be borrowed. */
esp_err_t candis_camera_frame_acquire(candis_camera_frame_t *frame);
esp_err_t candis_camera_frame_release(void);
/** Copy one frame into caller-owned storage. The stream stays active. */
esp_err_t candis_camera_capture_frame(void *buffer, size_t capacity, size_t *size);
/** Stop capture, invalidate any borrowed frame and release the stream resources. */
esp_err_t candis_camera_stream_stop(void);

/** Release camera hardware and any stream opened through this API.
 * Close native V4L2 file descriptors before calling. Safe to call again after success.
 */
esp_err_t candis_camera_stop(void);

#ifdef __cplusplus
}
#endif
