/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <inttypes.h>
#include "bsp/candis.h"
#include "camera_fixture.h"
#include "camera_pixels.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifdef CONFIG_CANDIS_CAMERA_SAVE_JPEG
#include "driver/jpeg_encode.h"
#include "still_jpeg.h"
#endif

#define SAMPLE_INTERVAL_US 5000000
#define STILL_WARMUP_FRAMES 10
static const char *TAG = "candis_camera_demo";

static esp_err_t check_preview_format(struct v4l2_pix_format *pix, size_t *frame_bytes)
{
    const bool yuv = pix->pixelformat == V4L2_PIX_FMT_UYVY || pix->pixelformat == V4L2_PIX_FMT_YUYV;
    const bool gray = pix->pixelformat == V4L2_PIX_FMT_GREY;
    const bool rgb565 = pix->pixelformat == V4L2_PIX_FMT_RGB565 || pix->pixelformat == V4L2_PIX_FMT_RGB565X;
    ESP_RETURN_ON_FALSE(pix->width >= 2 && pix->height >= 2 && pix->width <= UINT16_MAX &&
                        (gray || rgb565 || yuv) && (!yuv || !(pix->width & 1)),
                        ESP_ERR_NOT_SUPPORTED, TAG, "preview supports RGB565, YUV422, and grayscale DVP modes");
    ESP_RETURN_ON_FALSE(!yuv ||
                        ((pix->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT || pix->ycbcr_enc == V4L2_YCBCR_ENC_601) &&
                         (pix->quantization == V4L2_QUANTIZATION_DEFAULT ||
                          pix->quantization == V4L2_QUANTIZATION_FULL_RANGE ||
                          pix->quantization == V4L2_QUANTIZATION_LIM_RANGE)),
                        ESP_ERR_NOT_SUPPORTED, TAG, "unsupported YUV matrix or range");
    const size_t pixel_size = gray ? sizeof(uint8_t) : sizeof(uint16_t);
    ESP_RETURN_ON_FALSE(pix->width <= SIZE_MAX / pixel_size, ESP_ERR_INVALID_SIZE, TAG, "camera row size overflow");
    const size_t row_bytes = (size_t)pix->width * pixel_size;
    if (!pix->bytesperline) {
        ESP_RETURN_ON_FALSE(row_bytes <= UINT32_MAX, ESP_ERR_INVALID_SIZE, TAG, "camera stride overflow");
        pix->bytesperline = (uint32_t)row_bytes;
    }
    ESP_RETURN_ON_FALSE(pix->bytesperline >= row_bytes && (gray || pix->bytesperline % sizeof(uint16_t) == 0) &&
                        pix->bytesperline <= SIZE_MAX / pix->height, ESP_ERR_INVALID_SIZE, TAG, "invalid DVP stride");
    *frame_bytes = (size_t)(pix->height - 1) * pix->bytesperline + row_bytes;
    ESP_LOGI(TAG, "DVP %" PRIu32 "x%" PRIu32 " fourcc=0x%08" PRIx32 " stride=%" PRIu32
             ", preview %dx%d", pix->width, pix->height, pix->pixelformat, pix->bytesperline,
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

static void crop_and_scale(const struct v4l2_pix_format *pix, const uint8_t *src, bool swap,
                           const uint16_t columns[BSP_LCD_H_RES], uint16_t *dst)
{
    const bool yuyv = pix->pixelformat == V4L2_PIX_FMT_YUYV;
    const bool yuv = yuyv || pix->pixelformat == V4L2_PIX_FMT_UYVY;
    const bool gray = pix->pixelformat == V4L2_PIX_FMT_GREY;
    const bool limited = pix->quantization == V4L2_QUANTIZATION_LIM_RANGE;
    const unsigned crop_size = pix->width < pix->height ? pix->width : pix->height;
    const unsigned top = (pix->height - crop_size) / 2;
    for (unsigned y = 0; y < BSP_LCD_V_RES; ++y) {
        const unsigned source_y = top + y * crop_size / BSP_LCD_V_RES;
        const uint8_t *bytes = src + (size_t)source_y * pix->bytesperline;
        const uint16_t *row = (const uint16_t *)bytes;
        if (gray) {
            for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
                *dst++ = camera_gray_to_rgb565(bytes[columns[x]]);
            }
        } else if (yuv) {
            for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
                const unsigned col = columns[x];
                *dst++ = camera_yuv_pixel(bytes + (col & ~1U) * 2, yuyv, col & 1U, limited);
            }
        } else if (swap) {
            for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
                *dst++ = __builtin_bswap16(row[columns[x]]);
            }
        } else {
            for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
                *dst++ = row[columns[x]];
            }
        }
    }
}

static void prepare_crop(const struct v4l2_pix_format *format, uint16_t columns[BSP_LCD_H_RES])
{
    const unsigned crop_size = format->width < format->height ? format->width : format->height;
    const unsigned left = (format->width - crop_size) / 2;
    for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
        columns[x] = left + x * crop_size / BSP_LCD_H_RES;
    }
}

static esp_err_t preview(const struct v4l2_pix_format *pix, size_t frame_bytes, bool swap, uint16_t *pixels)
{
    uint16_t columns[BSP_LCD_H_RES];
    prepare_crop(pix, columns);

    candis_display_stats_t previous;
    ESP_RETURN_ON_ERROR(candis_display_get_stats(&previous), TAG, "get initial display stats");
    int64_t window_start = esp_timer_get_time();
    int64_t total_latency = 0, max_latency = 0;
    unsigned captured = 0, shown = 0, invalid = 0, consecutive_invalid = 0;
    bool first_frame = true;
    while (!candis_example_usb_download_pending()) {
        candis_camera_frame_t frame;
        ESP_RETURN_ON_ERROR(candis_camera_frame_acquire(&frame), TAG, "capture preview frame");
        const int64_t dequeued = esp_timer_get_time();
        ++captured;
        const bool valid = !(frame.flags & V4L2_BUF_FLAG_ERROR) && frame.size >= frame_bytes;
        if (valid) {
            crop_and_scale(pix, frame.data, swap, columns, pixels);
        }
        ESP_RETURN_ON_ERROR(candis_camera_frame_release(), TAG, "return preview frame");
        if (valid) {
            consecutive_invalid = 0;
            ESP_RETURN_ON_ERROR(candis_display_draw_rgb565(pixels), TAG, "send frame to AMOLED");
            const int64_t latency = esp_timer_get_time() - dequeued;
            total_latency += latency;
            max_latency = latency > max_latency ? latency : max_latency;
            ++shown;
            if (first_frame) {
                first_frame = false;
                ESP_LOGI(TAG, "First preview frame dequeue_to_dma_us=%" PRId64, latency);
            }
        } else {
            ++invalid;
            ESP_RETURN_ON_FALSE(++consecutive_invalid < 5, ESP_ERR_INVALID_SIZE, TAG, "repeated invalid DVP frames");
        }

        const int64_t now = esp_timer_get_time();
        const int64_t elapsed = now - window_start;
        if (elapsed >= SAMPLE_INTERVAL_US) {
            candis_display_stats_t stats;
            ESP_RETURN_ON_ERROR(candis_display_get_stats(&stats), TAG, "get display stats");
            ESP_LOGI(TAG, "Preview capture_fps=%.2f panel_fps=%.2f dequeue_to_dma_avg_us=%" PRId64
                     " dequeue_to_dma_max_us=%" PRId64 " transfer_us=%" PRIu32
                     " invalid=%u te_timeouts=%" PRIu32 " transfer_errors=%" PRIu32,
                     captured * 1000000.0 / elapsed, shown * 1000000.0 / elapsed,
                     shown ? total_latency / shown : 0, max_latency, stats.last_transfer_us, invalid,
                     stats.te_timeouts - previous.te_timeouts, stats.transfer_errors - previous.transfer_errors);
            previous = stats;
            window_start = now;
            captured = shown = invalid = 0;
            total_latency = max_latency = 0;
        }
    }
    return ESP_OK;
}

#ifdef CONFIG_CANDIS_CAMERA_YUYV_RGB565
static esp_err_t check_dvp_conversion(void)
{
    for (uint32_t index = 0; ; ++index) {
        struct v4l2_fmtdesc format;
        const esp_err_t err = candis_camera_get_pixel_format(index, &format);
        if (err == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (format.pixelformat == V4L2_PIX_FMT_RGB565X) {
            return ESP_OK;
        }
    }
}
#endif

static esp_err_t capture_payload(const struct v4l2_pix_format *format)
{
    candis_camera_frame_t frame;
    ESP_RETURN_ON_ERROR(candis_camera_frame_acquire(&frame), TAG, "capture DVP payload");
    const bool valid = frame.size && !(frame.flags & V4L2_BUF_FLAG_ERROR);
    ESP_RETURN_ON_ERROR(candis_camera_frame_release(), TAG, "release DVP payload");
    ESP_RETURN_ON_FALSE(valid, ESP_ERR_INVALID_RESPONSE, TAG, "invalid DVP payload");
    ESP_LOGI(TAG, "Captured %" PRIu32 "x%" PRIu32 " fourcc=0x%08" PRIx32 " bytes=%zu",
             format->width, format->height, format->pixelformat, frame.size);
    return ESP_OK;
}

static esp_err_t run_camera(uint16_t **preview_buffer)
{
    struct v4l2_pix_format format;
    candis_camera_stream_config_t config = {0};
#ifdef CONFIG_CANDIS_CAMERA_YUYV_RGB565
    ESP_RETURN_ON_ERROR(check_dvp_conversion(), TAG, "YUYV to RGB565X conversion unavailable");
    config.pixel_format = V4L2_PIX_FMT_RGB565X;
#endif
    ESP_RETURN_ON_ERROR(candis_camera_stream_start(&config, &format), TAG, "start preview stream");
#ifdef CONFIG_CANDIS_CAMERA_TEST_PATTERN
    ESP_RETURN_ON_ERROR(candis_camera_set_test_pattern(true), TAG, "enable sensor pattern");
#endif
    if (format.pixelformat == V4L2_PIX_FMT_SBGGR8 || format.pixelformat == V4L2_PIX_FMT_SBGGR10 ||
        format.pixelformat == V4L2_PIX_FMT_JPEG) {
        return capture_payload(&format);
    }
    size_t frame_bytes;
    ESP_RETURN_ON_ERROR(check_preview_format(&format, &frame_bytes), TAG, "configure preview");
    uint16_t *pixels = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(*pixels),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(pixels, ESP_ERR_NO_MEM, TAG, "allocate preview buffer");
    *preview_buffer = pixels;
    candis_display_config_t display = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_CAMERA_MANAGE_PANEL_POWER
    display.manage_panel_power = true;
#endif
#ifdef CONFIG_CANDIS_CAMERA_QSPI_50MHZ
    display.bench_qspi_80mhz = false;
#endif
    ESP_RETURN_ON_ERROR(candis_display_start_raw(&display), TAG, "start preview display");
#if defined(CONFIG_CANDIS_CAMERA_FIXTURE) && !defined(CONFIG_CANDIS_CAMERA_YUYV_RGB565)
    /* Preserve the byte order observed with the validation fixture. */
    const bool swap = format.pixelformat == V4L2_PIX_FMT_RGB565;
#else
    const bool swap = format.pixelformat == V4L2_PIX_FMT_RGB565X;
#endif
    bool still = false;
#ifdef CONFIG_CANDIS_CAMERA_STILL
    still = true;
#endif
    if (still) {
        esp_cam_sensor_id_t id;
        ESP_RETURN_ON_ERROR(candis_camera_read_sensor_id(&id), TAG, "read sensor identity");
        /* Leave the sensor controls at their current settings and let auto exposure settle. */
        for (unsigned i = 0; i < STILL_WARMUP_FRAMES; ++i) {
            candis_camera_frame_t frame;
            ESP_RETURN_ON_ERROR(candis_camera_frame_acquire(&frame), TAG, "wait for still frame");
            ESP_RETURN_ON_ERROR(candis_camera_frame_release(), TAG, "release warmup frame");
        }
        const size_t capacity = format.sizeimage > frame_bytes ? format.sizeimage : frame_bytes;
#ifdef CONFIG_CANDIS_CAMERA_SAVE_JPEG
        const jpeg_encode_memory_alloc_cfg_t mem = {.buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER};
        size_t allocated;
        uint8_t *raw = jpeg_alloc_encoder_mem(capacity, &mem, &allocated);
#else
        uint8_t *raw = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        ESP_RETURN_ON_FALSE(raw, ESP_ERR_NO_MEM, TAG, "allocate still");
        size_t len;
        esp_err_t err = candis_camera_capture_frame(raw, capacity, &len);
        if (err == ESP_OK && len < frame_bytes) {
            err = ESP_ERR_INVALID_SIZE;
        }
        if (err == ESP_OK) {
            uint16_t columns[BSP_LCD_H_RES];
            prepare_crop(&format, columns);
            crop_and_scale(&format, raw, swap, columns, pixels);
            err = candis_display_draw_rgb565(pixels);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Still frame: sensor=0x%04x bytes=%zu", id.pid, len);
#ifdef CONFIG_CANDIS_CAMERA_SAVE_JPEG
                err = candis_camera_stop();
                if (err == ESP_OK) {
                    err = camera_save_still_jpeg(&format, raw, swap);
                }
#endif
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }
        heap_caps_free(raw);
        return err;
    }
    return preview(&format, frame_bytes, swap, pixels);
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_example_usb_start(CANDIS_EXAMPLE_USB_CONSOLE, true));
#if defined(CONFIG_CANDIS_CAMERA_ENABLE_ALL_DVP_SENSORS)
    ESP_LOGI(TAG, "Select an individual sensor profile to run capture");
    return;
#elif !defined(CONFIG_CANDIS_CAMERA_FIXTURE) && !defined(CONFIG_CANDIS_CAMERA_DOCK_V12)
    ESP_LOGI(TAG, "Select a fixture or Dock v1.2 camera wiring profile to start capture");
    return;
#endif
    if (!candis_example_usb_begin_interruptible_operation()) {
        return;
    }
    uint16_t *pixels = NULL;
    esp_err_t err;
#ifdef CONFIG_CANDIS_CAMERA_FIXTURE
    const candis_camera_config_t camera = camera_fixture_config();
    err = candis_camera_init(&camera);
#elif defined(CONFIG_CANDIS_CAMERA_DOCK_V12)
    err = camera_dock_v12_init();
#else
    err = ESP_ERR_NOT_SUPPORTED;
#endif
    if (err == ESP_OK) {
        err = run_camera(&pixels);
    }
    esp_err_t camera_stop;
    esp_err_t display_stop;
    do {
        camera_stop = candis_camera_stop();
        display_stop = candis_display_stop();
        if (camera_stop != ESP_OK || display_stop != ESP_OK) {
            ESP_LOGE(TAG, "Resource release camera=%s display=%s",
                     esp_err_to_name(camera_stop), esp_err_to_name(display_stop));
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } while (camera_stop != ESP_OK || display_stop != ESP_OK);
    heap_caps_free(pixels);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Capture stopped: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Camera and display resources released");
    candis_example_usb_end_operation();
}
