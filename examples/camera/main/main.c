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
#ifdef CONFIG_CANDIS_CAMERA_CONTROL_CONSOLE
#include "control_console.h"
#endif
#ifdef CONFIG_CANDIS_CAMERA_MANUAL_CONTROLS
#include "bsp/candis_ov5640.h"
#endif
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
    ESP_RETURN_ON_FALSE(pix->width == 800 && pix->height == 600 &&
                        (yuv || pix->pixelformat == V4L2_PIX_FMT_RGB565 || pix->pixelformat == V4L2_PIX_FMT_RGB565X),
                        ESP_ERR_NOT_SUPPORTED, TAG, "expected 800x600 RGB565 or YUV422 DVP");
    ESP_RETURN_ON_FALSE(!yuv ||
                        ((pix->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT || pix->ycbcr_enc == V4L2_YCBCR_ENC_601) &&
                         (pix->quantization == V4L2_QUANTIZATION_DEFAULT ||
                          pix->quantization == V4L2_QUANTIZATION_FULL_RANGE ||
                          pix->quantization == V4L2_QUANTIZATION_LIM_RANGE)),
                        ESP_ERR_NOT_SUPPORTED, TAG, "unsupported YUV matrix or range");
    const uint32_t row_bytes = pix->width * sizeof(uint16_t);
    if (!pix->bytesperline) {
        pix->bytesperline = row_bytes;
    }
    ESP_RETURN_ON_FALSE(pix->bytesperline >= row_bytes && pix->bytesperline % sizeof(uint16_t) == 0 &&
                        pix->bytesperline <= SIZE_MAX / pix->height, ESP_ERR_INVALID_SIZE, TAG, "invalid DVP stride");
    *frame_bytes = (size_t)(pix->height - 1) * pix->bytesperline + row_bytes;
    ESP_LOGI(TAG, "DVP 800x600 fourcc=0x%08" PRIx32 " stride=%" PRIu32 ", preview %dx%d",
             pix->pixelformat, pix->bytesperline, BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

static void crop_and_scale(const struct v4l2_pix_format *pix, const uint8_t *src, bool swap,
                           const uint16_t columns[BSP_LCD_H_RES], uint16_t *dst)
{
    const bool yuyv = pix->pixelformat == V4L2_PIX_FMT_YUYV;
    const bool yuv = yuyv || pix->pixelformat == V4L2_PIX_FMT_UYVY;
    const bool limited = pix->quantization == V4L2_QUANTIZATION_LIM_RANGE;
    for (unsigned y = 0; y < BSP_LCD_V_RES; ++y) {
        const unsigned source_y = y * pix->height / BSP_LCD_V_RES;
        const uint8_t *bytes = src + (size_t)source_y * pix->bytesperline;
        const uint16_t *row = (const uint16_t *)bytes;
        if (yuv) {
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
    const unsigned left = (format->width - format->height) / 2;
    for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
        columns[x] = left + x * format->height / BSP_LCD_H_RES;
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
#ifdef CONFIG_CANDIS_CAMERA_CONTROL_CONSOLE
    camera_control_console_help();
#endif
    while (!candis_example_usb_download_pending()) {
#ifdef CONFIG_CANDIS_CAMERA_CONTROL_CONSOLE
        if (!camera_control_console_poll()) {
            return ESP_OK;
        }
#endif
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

#ifdef CONFIG_CANDIS_CAMERA_MANUAL_CONTROLS
static esp_err_t settle_control_group(void)
{
    /* Recycle both queued buffers and allow a further frame for the group update. */
    for (unsigned frame = 0; frame < 3; ++frame) {
        candis_camera_frame_t unused;
        ESP_RETURN_ON_ERROR(candis_camera_frame_acquire(&unused), TAG, "wait for updated controls");
        ESP_RETURN_ON_ERROR(candis_camera_frame_release(), TAG, "release settling frame");
    }
    return ESP_OK;
}

static esp_err_t apply_manual_controls(void)
{
    candis_ov5640_controls_t before;
    uint16_t frame_lines;
    ESP_RETURN_ON_ERROR(candis_ov5640_get_controls(&before, &frame_lines), TAG, "read OV5640 controls");
    const candis_ov5640_controls_t manual = {
        .exposure_lines = frame_lines / 4,
        .gain_q4 = 32,
        .red_gain_q10 = 1024,
        .green_gain_q10 = 1024,
        .blue_gain_q10 = 1024,
    };
    ESP_LOGI(TAG, "Initial controls AEC=%d AGC=%d AWB=%d frame_lines=%u",
             before.auto_exposure, before.auto_gain, before.auto_white_balance, (unsigned)frame_lines);
    ESP_RETURN_ON_ERROR(candis_ov5640_set_controls(&manual), TAG, "apply OV5640 controls");
    ESP_RETURN_ON_ERROR(settle_control_group(), TAG, "settle exposure and white balance");
    candis_ov5640_controls_t actual;
    ESP_RETURN_ON_ERROR(candis_ov5640_get_controls(&actual, NULL), TAG, "read back OV5640 controls");
    ESP_RETURN_ON_FALSE(!actual.auto_exposure && !actual.auto_gain && !actual.auto_white_balance &&
                        actual.exposure_lines == manual.exposure_lines && actual.gain_q4 == manual.gain_q4 &&
                        actual.red_gain_q10 == manual.red_gain_q10 && actual.green_gain_q10 == manual.green_gain_q10 &&
                        actual.blue_gain_q10 == manual.blue_gain_q10, ESP_ERR_INVALID_RESPONSE, TAG,
                        "manual controls did not read back");
    ESP_LOGI(TAG, "Manual controls readback exposure_lines=%u gain_q4=%u WB=%u/%u/%u",
             (unsigned)actual.exposure_lines, (unsigned)actual.gain_q4, (unsigned)actual.red_gain_q10,
             (unsigned)actual.green_gain_q10, (unsigned)actual.blue_gain_q10);
    const candis_ov5640_isp_t isp = {
        .luma_enabled = true, .saturation_enabled = true, .manual_saturation = true,
        .brightness = 16, .contrast = 0x20, .saturation_u = 0x40, .saturation_v = 0x40,
    };
    ESP_RETURN_ON_ERROR(candis_ov5640_set_isp(&isp), TAG, "apply ISP settings");
    ESP_RETURN_ON_ERROR(settle_control_group(), TAG, "settle ISP settings");
    candis_ov5640_isp_t applied;
    ESP_RETURN_ON_ERROR(candis_ov5640_get_isp(&applied), TAG, "read back ISP settings");
    ESP_RETURN_ON_FALSE(applied.luma_enabled && applied.saturation_enabled && applied.manual_saturation &&
                        applied.brightness == isp.brightness && applied.contrast == isp.contrast &&
                        applied.saturation_u == isp.saturation_u && applied.saturation_v == isp.saturation_v,
                        ESP_ERR_INVALID_RESPONSE, TAG, "ISP settings did not read back");
    ESP_LOGI(TAG, "ISP readback brightness=%d contrast=0x%02x U/V=0x%02x/0x%02x",
             applied.brightness, applied.contrast, applied.saturation_u, applied.saturation_v);
    return ESP_OK;
}
#endif

static esp_err_t run_preview(uint16_t *pixels)
{
    struct v4l2_pix_format format;
    candis_camera_stream_config_t config = {0};
#ifdef CONFIG_CANDIS_CAMERA_YUYV_RGB565
    ESP_RETURN_ON_ERROR(check_dvp_conversion(), TAG, "YUYV to RGB565X conversion unavailable");
    config.pixel_format = V4L2_PIX_FMT_RGB565X;
#endif
    ESP_RETURN_ON_ERROR(candis_camera_stream_start(&config, &format), TAG, "start preview stream");
    size_t frame_bytes;
    ESP_RETURN_ON_ERROR(check_preview_format(&format, &frame_bytes), TAG, "configure preview");
#ifdef CONFIG_CANDIS_CAMERA_YUYV_RGB565
    /* ESP Video documents big-endian RGB565X from the hardware converter. */
    const bool swap = true;
#else
    /* The fixture's direct OV5640 RGB565X profile has been observed low-byte first. */
    const bool swap = format.pixelformat == V4L2_PIX_FMT_RGB565;
#endif
#ifdef CONFIG_CANDIS_CAMERA_TEST_PATTERN
    ESP_RETURN_ON_ERROR(candis_camera_set_test_pattern(true), TAG, "enable sensor pattern");
#endif
#ifdef CONFIG_CANDIS_CAMERA_MANUAL_CONTROLS
    ESP_RETURN_ON_ERROR(apply_manual_controls(), TAG, "verify sensor controls before capture");
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
#ifndef CONFIG_CANDIS_CAMERA_FIXTURE
    ESP_LOGI(TAG, "Use sdkconfig.fixture with the Candis validation fixture to start the camera");
    return;
#endif
    if (!candis_example_usb_begin_interruptible_operation()) {
        return;
    }
    uint16_t *pixels = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(*pixels),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "allocate preview buffer");
        candis_example_usb_end_operation();
        return;
    }
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_CAMERA_MANAGE_PANEL_POWER
    config.manage_panel_power = true;
#endif
#ifdef CONFIG_CANDIS_CAMERA_QSPI_50MHZ
    config.bench_qspi_80mhz = false;
#endif
    esp_err_t err = candis_display_start_raw(&config);
    if (err == ESP_OK) {
        const candis_camera_config_t camera = camera_fixture_config();
        err = candis_camera_init(&camera);
        if (err == ESP_OK) {
            err = run_preview(pixels);
        }
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
