/* SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include "bsp/candis.h"
#include "camera_fixture.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define SAMPLE_INTERVAL_US 5000000
static const char *TAG = "candis_camera_demo";

static esp_err_t check_preview_format(struct v4l2_pix_format *pix, size_t *frame_bytes)
{
    ESP_RETURN_ON_FALSE(pix->width == 800 && pix->height == 600 &&
                        (pix->pixelformat == V4L2_PIX_FMT_RGB565 || pix->pixelformat == V4L2_PIX_FMT_RGB565X),
                        ESP_ERR_NOT_SUPPORTED, TAG, "expected 800x600 RGB565 DVP");
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

static void crop_and_scale(const struct v4l2_pix_format *pix, const uint8_t *src,
                           const uint16_t columns[BSP_LCD_H_RES], uint16_t *dst)
{
    /* OV5640 DVP in ESP Video 2.5.0 sends low bytes first for the default RGB565X
     * profile on this fixture. The Candis panel driver swaps native RGB565 for SPI. */
    const bool swap = pix->pixelformat == V4L2_PIX_FMT_RGB565;
    for (unsigned y = 0; y < BSP_LCD_V_RES; ++y) {
        const unsigned source_y = y * pix->height / BSP_LCD_V_RES;
        const uint16_t *row = (const uint16_t *)(src + (size_t)source_y * pix->bytesperline);
        if (swap) {
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

static esp_err_t preview(const struct v4l2_pix_format *pix, size_t frame_bytes, uint16_t *pixels)
{
    uint16_t columns[BSP_LCD_H_RES];
    const unsigned left = (pix->width - pix->height) / 2;
    for (unsigned x = 0; x < BSP_LCD_H_RES; ++x) {
        columns[x] = left + x * pix->height / BSP_LCD_H_RES;
    }

    candis_display_stats_t previous;
    ESP_RETURN_ON_ERROR(candis_display_get_stats(&previous), TAG, "get initial display stats");
    int64_t window_start = esp_timer_get_time();
    int64_t total_latency = 0, max_latency = 0;
    unsigned captured = 0, shown = 0, invalid = 0, consecutive_invalid = 0;
    bool first_frame = true;
    for (;;) {
        candis_camera_frame_t frame;
        ESP_RETURN_ON_ERROR(candis_camera_frame_acquire(&frame), TAG, "capture preview frame");
        const int64_t dequeued = esp_timer_get_time();
        ++captured;
        const bool valid = !(frame.flags & V4L2_BUF_FLAG_ERROR) && frame.size >= frame_bytes;
        if (valid) {
            crop_and_scale(pix, frame.data, columns, pixels);
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
}

static esp_err_t run_preview(uint16_t *pixels)
{
    struct v4l2_pix_format format;
    ESP_RETURN_ON_ERROR(candis_camera_stream_start(NULL, &format), TAG, "start preview stream");
    size_t frame_bytes;
    ESP_RETURN_ON_ERROR(check_preview_format(&format, &frame_bytes), TAG, "configure preview");
    return preview(&format, frame_bytes, pixels);
}

void app_main(void)
{
    uint16_t *pixels = heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(*pixels),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "allocate preview buffer");
        return;
    }
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
#ifdef CONFIG_CANDIS_CAMERA_MANAGE_PANEL_POWER
    config.manage_panel_power = true;
#endif
#ifdef CONFIG_CANDIS_CAMERA_QSPI_80MHZ
    config.bench_qspi_80mhz = true;
#endif
    esp_err_t err = candis_display_start_raw(&config);
    if (err == ESP_OK) {
        const candis_camera_config_t camera = camera_fixture_config();
        err = candis_camera_init(&camera);
        if (err == ESP_OK) {
            err = run_preview(pixels);
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(candis_camera_stop());
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(candis_display_stop());
    heap_caps_free(pixels);
    ESP_LOGE(TAG, "Preview stopped: %s", esp_err_to_name(err));
}
