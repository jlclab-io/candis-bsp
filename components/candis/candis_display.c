/* SPDX-License-Identifier: Apache-2.0 */
/* Candis 1.2, AM214Q480480LK2 module (ST7801N and CST9220). */
#include <stdatomic.h>
#include <string.h>
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define PANEL_HOST SPI2_HOST
#define PANEL_RESET GPIO_NUM_2
#define PANEL_TE GPIO_NUM_8
#define DRAW_ROWS 32
#define DRAW_BYTES (BSP_LCD_H_RES * DRAW_ROWS * sizeof(uint16_t))
#define FRAME_PIXELS (BSP_LCD_H_RES * BSP_LCD_V_RES)
#define TOUCH_POINTS 2
_Static_assert(DRAW_BYTES < 32768 && DRAW_ROWS % 2 == 0, "ST7801/S31 strip size");
static const char *TAG = "candis_display";
static atomic_bool s_port_ready;
static atomic_bool s_active;
static atomic_bool s_raw_active;
static atomic_uint s_te_edges;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    candis_power_t power;
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *input;
    lv_timer_t *retry;
    SemaphoreHandle_t te;
    SemaphoreHandle_t dma;
    uint16_t *frame;
    uint8_t *draw;
    uint8_t *transfer[2];
    lv_area_t dirty;
    int64_t refresh_start_us;
    candis_touch_point_t points[TOUCH_POINTS];
    candis_display_stats_t stats;
    SemaphoreHandle_t panel_idle;
    TaskHandle_t panel_task;
    uint16_t *scanout;
    lv_area_t submitted;
    bool stale_rows[BSP_LCD_V_RES];
    uint32_t prepare_us;
    uint32_t submitted_prepare_us;
    bool submitted_failed;
    bool panel_stopping;
    uint8_t point_count;
    bool dirty_valid;
    bool sync_to_te;
    bool dma_pending;
    bool spi;
    bool te_pin;
    bool te_handler;
    bool reset_pin;
    bool panel_awake;
} s_display;

static esp_err_t panel_command(uint8_t command, const void *data, size_t size)
{
    return esp_lcd_panel_io_tx_param(s_display.panel_io, (0x02 << 24) | (command << 8), data, size);
}

static esp_err_t panel_brightness(unsigned percent)
{
    uint8_t level = percent * 255 / 100;
    return panel_command(LCD_CMD_WRDISBV, &level, 1);
}

static esp_err_t panel_draw(const lv_area_t *area, int top, int bottom, const void *buffer)
{
    /* Exclusive extents are even, as required by the controller's address window. */
    const uint8_t columns[] = {area->x1 >> 8, area->x1, area->x2 >> 8, area->x2};
    const int last = bottom - 1;
    const uint8_t rows[] = {top >> 8, top, last >> 8, last};
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_CASET, columns, sizeof(columns)), TAG, "CASET");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_RASET, rows, sizeof(rows)), TAG, "RASET");
    return esp_lcd_panel_io_tx_color(s_display.panel_io, (0x32 << 24) | (LCD_CMD_RAMWR << 8),
                                     buffer, lv_area_get_width(area) * (bottom - top) * 2U);
}

static void IRAM_ATTR te_edge(void *context)
{
    atomic_fetch_add_explicit(&s_te_edges, 1, memory_order_relaxed);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    if (wake) {
        portYIELD_FROM_ISR();
    }
}

static bool dma_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *context)
{
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)context, &wake);
    return wake == pdTRUE;
}

static esp_err_t panel_init(const candis_display_config_t *config)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = GPIO_NUM_12,
        .data0_io_num = GPIO_NUM_11, .data1_io_num = GPIO_NUM_13,
        .data2_io_num = GPIO_NUM_14, .data3_io_num = GPIO_NUM_9,
        .data4_io_num = -1, .data5_io_num = -1, .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = DRAW_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(PANEL_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "init QSPI");
    s_display.spi = true;
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = GPIO_NUM_10, .dc_gpio_num = -1,
        .pclk_hz = config->bench_qspi_80mhz ? 80000000 : 50000000,
        .trans_queue_depth = 1,
        .on_color_trans_done = dma_done, .user_ctx = s_display.dma,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)PANEL_HOST, &io,
                                                &s_display.panel_io), TAG, "init panel IO");
    const gpio_config_t reset = {.pin_bit_mask = 1ULL << PANEL_RESET, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(PANEL_RESET, 0), TAG, "preset reset output");
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "init panel reset");
    s_display.reset_pin = true;
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    ESP_RETURN_ON_ERROR(gpio_set_level(PANEL_RESET, 1), TAG, "release panel reset");
    vTaskDelay(pdMS_TO_TICKS(150) + 1);

    /* AM214Q480480LK2 power-up command order from the validated Bring-Up settings. */
    const uint8_t window[] = {0, 0, 1, 0xdf};
    const uint8_t rgb565 = 0x55, normal_brightness = 0x20, te_mode = 0;
    const uint8_t default_scan = 0, reverse_rows = 0x80;
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_MADCTL, &default_scan, 1), TAG, "MADCTL");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_COLMOD, &rgb565, 1), TAG, "COLMOD");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_SLPOUT, NULL, 0), TAG, "SLPOUT");
    s_display.panel_awake = true;
    vTaskDelay(pdMS_TO_TICKS(200) + 1);
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_CASET, window, sizeof(window)), TAG, "CASET");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_RASET, window, sizeof(window)), TAG, "RASET");
    ESP_RETURN_ON_ERROR(panel_command(0x53, &normal_brightness, 1), TAG, "brightness mode");
    ESP_RETURN_ON_ERROR(panel_brightness(config->brightness_percent), TAG, "brightness");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_COLMOD, &rgb565, 1), TAG, "COLMOD");
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_TEON, &te_mode, 1), TAG, "TEON");
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_MADCTL, &reverse_rows, 1), TAG, "MADCTL MY");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_DISPON, NULL, 0), TAG, "DISPON");

    const gpio_config_t te = {
        .pin_bit_mask = 1ULL << PANEL_TE, .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "init TE pin");
    s_display.te_pin = true;
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PANEL_TE, te_edge, s_display.te), TAG, "register TE ISR");
    s_display.te_handler = true;
    return ESP_OK;
}

static void round_area(lv_event_t *event)
{
    lv_area_t *area = lv_event_get_param(event);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void mark_refresh_start(lv_event_t *event)
{
    s_display.refresh_start_us = esp_timer_get_time();
}

static bool drain_dma(void)
{
    if (s_display.dma_pending) {
        if (!xSemaphoreTake(s_display.dma, pdMS_TO_TICKS(100) + 1)) {
            return false;
        }
        s_display.dma_pending = false;
    }
    return true;
}

static bool send_dirty_area(const uint16_t *frame, const lv_area_t *dirty)
{
    if (!drain_dma()) {
        return false;
    }
    unsigned slot = 0;
    const size_t width = lv_area_get_width(dirty);
    /* With MADCTL MY, send bottom strips first. Stage the next strip while DMA runs. */
    for (int bottom = dirty->y2 + 1; bottom > dirty->y1;) {
        const int top = LV_MAX(dirty->y1, bottom - DRAW_ROWS);
        uint8_t *buffer = s_display.transfer[slot];
        for (int y = top; y < bottom; ++y) {
            uint8_t *dst = buffer + (y - top) * width * 2;
            const uint16_t *src = frame + y * BSP_LCD_H_RES + dirty->x1;
            memcpy(dst, src, width * sizeof(*src));
        }
        lv_draw_sw_rgb565_swap(buffer, width * (bottom - top));
        if (!drain_dma()) {
            return false;
        }
        if (panel_draw(dirty, top, bottom, buffer) != ESP_OK) {
            return false;
        }
        s_display.dma_pending = true;
        bottom = top;
        slot ^= 1;
    }
    return drain_dma();
}

static bool transfer_frame(const uint16_t *frame, const lv_area_t *dirty, uint32_t prepare_us)
{
    const int64_t wait_start = esp_timer_get_time();
    bool te_timeout = false;
    if (s_display.sync_to_te) {
        xSemaphoreTake(s_display.te, 0);
        te_timeout = !xSemaphoreTake(s_display.te, pdMS_TO_TICKS(40) + 1);
    }
    const int64_t transfer_start = esp_timer_get_time();
    const bool sent = send_dirty_area(frame, dirty);
    const uint32_t transfer_us = esp_timer_get_time() - transfer_start;
    portENTER_CRITICAL(&s_stats_lock);
    s_display.stats.te_timeouts += te_timeout;
    s_display.stats.te_wait_us += transfer_start - wait_start;
    if (sent) {
        ++s_display.stats.frames;
        s_display.stats.prepare_us += prepare_us;
        s_display.stats.pixels += lv_area_get_size(dirty);
        s_display.stats.transfer_us += transfer_us;
        s_display.stats.last_transfer_us = transfer_us;
        s_display.stats.max_transfer_us = LV_MAX(s_display.stats.max_transfer_us, transfer_us);
    }
    bool log_error = false;
    if (!sent) {
        log_error = s_display.stats.transfer_errors % 100 == 0;
        ++s_display.stats.transfer_errors;
    }
    portEXIT_CRITICAL(&s_stats_lock);
    if (log_error) {
        ESP_LOGW(TAG, "display transfer failed; retrying on refresh");
    }
    return sent;
}

static void panel_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_display.panel_stopping) {
            xSemaphoreGive(s_display.panel_idle);
            vTaskDelete(NULL);
            return;
        }
        s_display.submitted_failed = !transfer_frame(s_display.scanout, &s_display.submitted,
                                                     s_display.submitted_prepare_us);
        xSemaphoreGive(s_display.panel_idle);
    }
}

static void add_dirty(const lv_area_t *area)
{
    if (s_display.dirty_valid) {
        s_display.dirty.x1 = LV_MIN(s_display.dirty.x1, area->x1);
        s_display.dirty.y1 = LV_MIN(s_display.dirty.y1, area->y1);
        s_display.dirty.x2 = LV_MAX(s_display.dirty.x2, area->x2);
        s_display.dirty.y2 = LV_MAX(s_display.dirty.y2, area->y2);
    } else {
        s_display.dirty = *area;
        s_display.dirty_valid = true;
    }
}

static void sync_row(unsigned row)
{
    const size_t offset = row * BSP_LCD_H_RES + s_display.submitted.x1;
    memcpy(s_display.frame + offset, s_display.scanout + offset,
           lv_area_get_width(&s_display.submitted) * sizeof(uint16_t));
}

static void submit_frame(void)
{
    if (!xSemaphoreTake(s_display.panel_idle, 0)) {
        lv_timer_resume(s_display.retry);
        return;
    }
    if (s_display.submitted_failed) {
        add_dirty(&s_display.submitted);
        s_display.submitted_failed = false;
    }
    if (!s_display.dirty_valid) {
        xSemaphoreGive(s_display.panel_idle);
        lv_timer_pause(s_display.retry);
        return;
    }
    const int64_t started = esp_timer_get_time();
    const lv_area_t dirty = s_display.dirty;
    for (int row = s_display.submitted.y1; row <= s_display.submitted.y2; ++row) {
        if (s_display.stale_rows[row]) {
            sync_row(row);
            s_display.stale_rows[row] = false;
        }
    }
    uint16_t *previous = s_display.scanout;
    s_display.scanout = s_display.frame;
    s_display.frame = previous;
    memset(s_display.stale_rows + dirty.y1, 1, lv_area_get_height(&dirty));
    /* The worker owns the snapshot until it releases panel_idle. */
    s_display.submitted = dirty;
    s_display.submitted_prepare_us = s_display.prepare_us + (esp_timer_get_time() - started);
    s_display.dirty_valid = false;
    lv_timer_resume(s_display.retry);
    xTaskNotifyGive(s_display.panel_task);
}

static void flush_display(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const int width = lv_area_get_width(area);
    const unsigned stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; ++y) {
        /* Reuse the previous frame only where this row will not be overwritten. */
        if (s_display.stale_rows[y]) {
            if (area->x1 > s_display.submitted.x1 || area->x2 < s_display.submitted.x2) {
                sync_row(y);
            }
            s_display.stale_rows[y] = false;
        }
        const uint16_t *src = (const uint16_t *)(pixels + (y - area->y1) * stride);
        uint16_t *dst = s_display.frame + y * BSP_LCD_H_RES + area->x1;
        memcpy(dst, src, width * sizeof(*dst));
    }
    add_dirty(area);
    if (lv_display_flush_is_last(display)) {
        s_display.prepare_us = esp_timer_get_time() - s_display.refresh_start_us;
        if (s_display.panel_task) {
            submit_frame();
        } else if (transfer_frame(s_display.frame, &s_display.dirty, s_display.prepare_us)) {
            lv_timer_pause(s_display.retry);
            s_display.dirty_valid = false;
        } else {
            lv_timer_resume(s_display.retry);
        }
    }
    lv_display_flush_ready(display);
}

static void read_touch(lv_indev_t *input, lv_indev_data_t *data)
{
    esp_err_t err = esp_lcd_touch_read_data(s_display.touch);
    if (err != ESP_ERR_NOT_FINISHED) {
        esp_lcd_touch_point_data_t points[TOUCH_POINTS];
        uint8_t count = 0;
        if (err == ESP_OK) {
            err = esp_lcd_touch_get_data(s_display.touch, points, &count, TOUCH_POINTS);
        }
        if (err != ESP_OK) {
            ++s_display.stats.touch_errors;
            count = 0;
        }
        for (uint8_t i = 0; i < count; ++i) {
            s_display.points[i] = (candis_touch_point_t){points[i].x, points[i].y, points[i].track_id};
        }
        s_display.point_count = count;
    }
    data->state = s_display.point_count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point = (lv_point_t){s_display.points[0].x, s_display.points[0].y};
}

static esp_err_t touch_init(void)
{
    const esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_display.power.bus, &io, &s_display.touch_io),
                        TAG, "init touch IO");
    const esp_lcd_touch_config_t config = {
        .x_max = BSP_LCD_H_RES, .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_3, .int_gpio_num = GPIO_NUM_1,
        .flags.mirror_x = true, .flags.mirror_y = true,
    };
    return esp_lcd_touch_new_i2c_cst9220(s_display.touch_io, &config, &s_display.touch);
}

static void retry_refresh(lv_timer_t *timer)
{
    if (s_display.panel_task) {
        submit_frame();
        return;
    }
    lv_display_t *display = lv_timer_get_user_data(timer);
    lv_obj_invalidate(lv_display_get_screen_active(display));
}

static esp_err_t ui_create(const candis_display_config_t *config)
{
    if (!atomic_load(&s_port_ready)) {
        lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
        port.task_stack = 6144;
        port.task_affinity = 0;
        ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "start LVGL task");
        atomic_store(&s_port_ready, true);
    }
    if (!lvgl_port_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    s_display.display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (s_display.display) {
        lv_display_set_color_format(s_display.display, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(s_display.display, s_display.draw, NULL, DRAW_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(s_display.display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
        lv_display_add_event_cb(s_display.display, mark_refresh_start, LV_EVENT_REFR_START, NULL);
        lv_display_set_flush_cb(s_display.display, flush_display);
        lv_obj_t *screen = lv_display_get_screen_active(s_display.display);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        s_display.retry = lv_timer_create(retry_refresh, s_display.panel_task ? 5 : 100, s_display.display);
        if (s_display.retry) {
            lv_timer_pause(s_display.retry);
            if (config->enable_touch) {
                s_display.input = lv_indev_create();
                if (s_display.input) {
                    lv_indev_set_type(s_display.input, LV_INDEV_TYPE_POINTER);
                    lv_indev_set_display(s_display.input, s_display.display);
                    lv_indev_set_read_cb(s_display.input, read_touch);
                }
            }
            if (!config->enable_touch || s_display.input) {
                atomic_store(&s_active, true);
                err = ESP_OK;
            }
        }
        if (err != ESP_OK) {
            lv_display_set_flush_cb(s_display.display, NULL);
            if (s_display.retry) {
                lv_timer_delete(s_display.retry);
                s_display.retry = NULL;
            }
            lv_display_delete(s_display.display);
            s_display.display = NULL;
        }
    }
    lvgl_port_unlock();
    return err;
}

static esp_err_t release_display(void)
{
    atomic_store(&s_raw_active, false);
    if (s_display.display) {
        if (!lvgl_port_lock(2000)) {
            return ESP_ERR_TIMEOUT;
        }
        atomic_store(&s_active, false);
        if (s_display.input) {
            lv_indev_delete(s_display.input);
            s_display.input = NULL;
        }
        lv_timer_delete(s_display.retry);
        s_display.retry = NULL;
        lv_display_delete(s_display.display);
        s_display.display = NULL;
        lvgl_port_unlock();
    }
    if (s_display.panel_task) {
        const TickType_t timeout = pdMS_TO_TICKS(2000) + 1;
        if (!s_display.panel_stopping) {
            ESP_RETURN_ON_FALSE(xSemaphoreTake(s_display.panel_idle, timeout), ESP_ERR_TIMEOUT,
                                TAG, "panel transfer still running");
            s_display.panel_stopping = true;
            xTaskNotifyGive(s_display.panel_task);
        }
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_display.panel_idle, timeout), ESP_ERR_TIMEOUT,
                            TAG, "panel task still running");
        s_display.panel_task = NULL;
    }
    ESP_RETURN_ON_FALSE(drain_dma(), ESP_ERR_TIMEOUT, TAG, "panel DMA still pending");
    if (s_display.te_handler) {
        ESP_RETURN_ON_ERROR(gpio_isr_handler_remove(PANEL_TE), TAG, "remove TE handler");
        s_display.te_handler = false;
    }
    if (s_display.te_pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(PANEL_TE), TAG, "release TE pin");
        s_display.te_pin = false;
    }
    if (s_display.touch) {
        ESP_RETURN_ON_ERROR(esp_lcd_touch_del(s_display.touch), TAG, "release touch");
        s_display.touch = NULL;
    }
    if (s_display.touch_io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_display.touch_io), TAG, "release touch IO");
        s_display.touch_io = NULL;
    }
    if (s_display.panel_awake) {
        ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_DISPOFF, NULL, 0), TAG, "DISPOFF");
        ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_SLPIN, NULL, 0), TAG, "SLPIN");
        vTaskDelay(pdMS_TO_TICKS(120) + 1);
        s_display.panel_awake = false;
    }
    if (s_display.panel_io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_display.panel_io), TAG, "release panel IO");
        s_display.panel_io = NULL;
    }
    if (s_display.spi) {
        ESP_RETURN_ON_ERROR(spi_bus_free(PANEL_HOST), TAG, "release QSPI");
        s_display.spi = false;
    }
    if (s_display.reset_pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(PANEL_RESET), TAG, "release reset pin");
        s_display.reset_pin = false;
    }
    ESP_RETURN_ON_ERROR(candis_power_release(&s_display.power), TAG, "release panel rail");
    if (s_display.te) {
        vSemaphoreDelete(s_display.te);
    }
    if (s_display.dma) {
        vSemaphoreDelete(s_display.dma);
    }
    if (s_display.panel_idle) {
        vSemaphoreDelete(s_display.panel_idle);
    }
    heap_caps_free(s_display.scanout);
    heap_caps_free(s_display.transfer[0]);
    heap_caps_free(s_display.transfer[1]);
    heap_caps_free(s_display.draw);
    heap_caps_free(s_display.frame);
    memset(&s_display, 0, sizeof(s_display));
    return ESP_OK;
}

static esp_err_t start_display(const candis_display_config_t *config, bool raw)
{
    s_display.sync_to_te = config->sync_to_te;
    atomic_store(&s_te_edges, 0);
    if (!raw) {
        s_display.frame = heap_caps_calloc(FRAME_PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_display.draw = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, DRAW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        ESP_RETURN_ON_FALSE(s_display.frame && s_display.draw, ESP_ERR_NO_MEM, TAG, "LVGL buffers");
    }
    s_display.transfer[0] = heap_caps_aligned_alloc(64, DRAW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_display.transfer[1] = heap_caps_aligned_alloc(64, DRAW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_display.te = xSemaphoreCreateBinary();
    s_display.dma = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_display.transfer[0] && s_display.transfer[1] &&
                        s_display.te && s_display.dma, ESP_ERR_NO_MEM, TAG, "display buffers or semaphores");
    s_display.power.rail = CANDIS_RAIL_DISPLAY;
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_display.power, config->manage_panel_power, 150),
                        TAG, "prepare panel rail");
    ESP_RETURN_ON_ERROR(panel_init(config), TAG, "start panel");
    if (raw) {
        atomic_store(&s_raw_active, true);
        return ESP_OK;
    }
    if (config->enable_touch) {
        ESP_RETURN_ON_ERROR(touch_init(), TAG, "start touch");
    }
    if (config->async_flush) {
        s_display.scanout = heap_caps_calloc(FRAME_PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_display.panel_idle = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_display.scanout && s_display.panel_idle, ESP_ERR_NO_MEM, TAG, "panel snapshot");
        xSemaphoreGive(s_display.panel_idle);
        const BaseType_t core = configNUMBER_OF_CORES > 1 ? 1 : 0;
        ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(panel_task, "candis_panel", 4096, NULL, 5,
                                                   &s_display.panel_task, core) == pdPASS,
                            ESP_ERR_NO_MEM, TAG, "panel task");
    }
    return ui_create(config);
}

static esp_err_t open_display(const candis_display_config_t *config, lv_display_t **display)
{
    if (!config || config->brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (display) {
        *display = NULL;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_load(&s_active) || atomic_load(&s_raw_active) || s_display.frame || s_display.te || s_display.dma) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_display(config, display == NULL);
    if (err == ESP_OK) {
        if (display) {
            *display = s_display.display;
        }
    } else {
        esp_err_t cleanup = release_display();
        if (cleanup != ESP_OK) {
            ESP_LOGE(TAG, "startup cleanup: %s; retry candis_display_stop", esp_err_to_name(cleanup));
        }
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_display_start(const candis_display_config_t *config, lv_display_t **display)
{
    return display ? open_display(config, display) : ESP_ERR_INVALID_ARG;
}

esp_err_t candis_display_start_raw(const candis_display_config_t *config)
{
    return open_display(config, NULL);
}

esp_err_t candis_display_draw_rgb565(const uint16_t *pixels)
{
    if (!pixels) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (atomic_load(&s_raw_active)) {
        const lv_area_t full = {.x2 = BSP_LCD_H_RES - 1, .y2 = BSP_LCD_V_RES - 1};
        err = transfer_frame(pixels, &full, 0) ? ESP_OK : ESP_FAIL;
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_display_stop(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = release_display();
    candis_control_leave();
    return err;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    if (!atomic_load(&s_port_ready) || !lvgl_port_lock(timeout_ms)) {
        return false;
    }
    if (!atomic_load(&s_active)) {
        lvgl_port_unlock();
        return false;
    }
    return true;
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}

lv_display_t *bsp_display_start(void)
{
    const candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    lv_display_t *display = NULL;
    (void)candis_display_start(&config, &display);
    return display;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent < 0 || brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bsp_display_lock(2000)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_TIMEOUT;
    if (!s_display.panel_idle || xSemaphoreTake(s_display.panel_idle, pdMS_TO_TICKS(200) + 1)) {
        err = drain_dma() ? panel_brightness(brightness_percent) : ESP_ERR_TIMEOUT;
        if (s_display.panel_idle) {
            xSemaphoreGive(s_display.panel_idle);
        }
    }
    bsp_display_unlock();
    return err;
}

esp_err_t candis_display_get_stats(candis_display_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool raw = atomic_load(&s_raw_active);
    if (raw) {
        if (!candis_control_enter()) {
            return ESP_ERR_INVALID_STATE;
        }
        if (!atomic_load(&s_raw_active)) {
            candis_control_leave();
            return ESP_ERR_INVALID_STATE;
        }
    } else if (!bsp_display_lock(2000)) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_display.stats;
    portEXIT_CRITICAL(&s_stats_lock);
    stats->te_edges = atomic_load_explicit(&s_te_edges, memory_order_relaxed);
    if (raw) {
        candis_control_leave();
    } else {
        bsp_display_unlock();
    }
    return ESP_OK;
}

uint8_t candis_display_get_touch_points(candis_touch_point_t points[2])
{
    if (!points) {
        return 0;
    }
    memcpy(points, s_display.points, s_display.point_count * sizeof(*points));
    return s_display.point_count;
}
