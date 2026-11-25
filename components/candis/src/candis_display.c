/* SPDX-License-Identifier: Apache-2.0 */
/* Candis 1.2, AM214Q480480LK2 module (ST7801N and CST9220). */
#include <stdatomic.h>
#include <string.h>
#include "candis_internal.h"
#include "bsp/touch.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DRAW_ROWS 32
#define DRAW_BYTES (BSP_LCD_H_RES * DRAW_ROWS * sizeof(uint16_t))
#define FRAME_PIXELS (BSP_LCD_H_RES * BSP_LCD_V_RES)
#define TOUCH_POINTS 2
_Static_assert(DRAW_BYTES < 32768 && DRAW_ROWS % 2 == 0, "ST7801/S31 strip size");
static const char *TAG = "candis_display";
#if !BSP_CONFIG_NO_GRAPHIC_LIB
typedef lv_area_t panel_area_t;
static _Atomic(SemaphoreHandle_t) s_ui_lock;
static StaticSemaphore_t s_ui_lock_storage;
static atomic_bool s_active;
static atomic_bool s_touch_pending;
#else
typedef struct {
    int x1, y1, x2, y2;
} panel_area_t;
#endif
static atomic_bool s_raw_active;
static atomic_uint s_te_edges;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    candis_power_t power;
    esp_lcd_panel_io_handle_t panel_io;
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    esp_lcd_panel_io_handle_t touch_io;
    esp_lcd_touch_handle_t touch;
    lv_display_t *display;
    lv_indev_t *input;
    lv_timer_t *retry;
    uint16_t *frame;
    uint8_t *draw;
    uint8_t *draw_second;
    size_t draw_bytes;
    TaskHandle_t ui_task;
    SemaphoreHandle_t ui_done;
    uint32_t max_sleep_ms;
    bool lvgl_initialized;
    lv_area_t dirty;
    int64_t refresh_start_us;
    candis_touch_point_t points[TOUCH_POINTS];
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
    uint8_t touch_read_failures;
    bool dirty_valid;
#endif
    SemaphoreHandle_t te;
    SemaphoreHandle_t dma;
    unsigned brightness;
    unsigned last_nonzero_brightness;
    bool exposed_panel;
    bool panel_visible;
    uint8_t *transfer[2];
    candis_display_stats_t stats;
    bool sync_to_te;
    bool dma_pending;
    bool spi;
    bool te_pin;
    bool te_handler;
    bool reset_pin;
    bool panel_awake;
    bool sleeping;
    bool sleep_pending;
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

static size_t panel_width(const panel_area_t *area)
{
    return area->x2 - area->x1 + 1;
}

static esp_err_t panel_draw(const panel_area_t *area, int top, int bottom, const void *buffer)
{
    /* Exclusive extents are even, as required by the controller's address window. */
    const uint8_t columns[] = {area->x1 >> 8, area->x1, area->x2 >> 8, area->x2};
    const int last = bottom - 1;
    const uint8_t rows[] = {top >> 8, top, last >> 8, last};
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_CASET, columns, sizeof(columns)), TAG, "CASET");
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_RASET, rows, sizeof(rows)), TAG, "RASET");
    return esp_lcd_panel_io_tx_color(s_display.panel_io, (0x32 << 24) | (LCD_CMD_RAMWR << 8),
                                     buffer, panel_width(area) * (bottom - top) * 2U);
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
        .sclk_io_num = CANDIS_LCD_CLK,
        .data0_io_num = CANDIS_LCD_D0, .data1_io_num = CANDIS_LCD_D1,
        .data2_io_num = CANDIS_LCD_D2, .data3_io_num = CANDIS_LCD_D3,
        .data4_io_num = -1, .data5_io_num = -1, .data6_io_num = -1, .data7_io_num = -1,
        .max_transfer_sz = DRAW_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(CANDIS_LCD_SPI, &bus, SPI_DMA_CH_AUTO), TAG, "init QSPI");
    s_display.spi = true;
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = CANDIS_LCD_CS, .dc_gpio_num = -1,
        .pclk_hz = config->bench_qspi_80mhz ? 80000000 : 50000000,
        .trans_queue_depth = 1,
        .on_color_trans_done = dma_done, .user_ctx = s_display.dma,
        .lcd_cmd_bits = 32, .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CANDIS_LCD_SPI, &io,
                                                &s_display.panel_io), TAG, "init panel IO");
    const gpio_config_t reset = {.pin_bit_mask = 1ULL << CANDIS_LCD_RESET, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_LCD_RESET, 0), TAG, "preset reset output");
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "init panel reset");
    s_display.reset_pin = true;
    vTaskDelay(pdMS_TO_TICKS(10) + 1);
    ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_LCD_RESET, 1), TAG, "release panel reset");
    vTaskDelay(pdMS_TO_TICKS(150) + 1);

    /* AM214Q480480LK2 power-up command sequence for ST7801N controller. */
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
    if (!s_display.exposed_panel) {
        ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_DISPON, NULL, 0), TAG, "DISPON");
        s_display.panel_visible = true;
    }

    const gpio_config_t te = {
        .pin_bit_mask = 1ULL << CANDIS_LCD_TE, .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&te), TAG, "init TE pin");
    s_display.te_pin = true;
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CANDIS_LCD_TE, te_edge, s_display.te), TAG, "register TE ISR");
    s_display.te_handler = true;
    return ESP_OK;
}

#if !BSP_CONFIG_NO_GRAPHIC_LIB
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
#endif

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

static bool send_dirty_area(const uint16_t *frame, const panel_area_t *dirty, bool packed)
{
    if (!drain_dma()) {
        return false;
    }
    unsigned slot = 0;
    const size_t width = panel_width(dirty);
    /* With MADCTL MY, send bottom strips first. Stage the next strip while DMA runs. */
    for (int bottom = dirty->y2 + 1; bottom > dirty->y1;) {
        const int top = bottom - dirty->y1 > DRAW_ROWS ? bottom - DRAW_ROWS : dirty->y1;
        uint8_t *buffer = s_display.transfer[slot];
        for (int y = top; y < bottom; ++y) {
            uint8_t *dst = buffer + (y - top) * width * 2;
            const uint16_t *src = packed ? frame + (y - dirty->y1) * width : frame + y * BSP_LCD_H_RES + dirty->x1;
            memcpy(dst, src, width * sizeof(*src));
        }
#if BSP_CONFIG_NO_GRAPHIC_LIB
        uint16_t *native = (uint16_t *)buffer;
        for (size_t i = 0; i < width * (bottom - top); ++i) {
            native[i] = __builtin_bswap16(native[i]);
        }
#else
        lv_draw_rgb565_swap(buffer, width * (bottom - top));
#endif
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

static bool transfer_frame(const uint16_t *frame, const panel_area_t *dirty, uint32_t prepare_us, bool packed)
{
    const int64_t wait_start = esp_timer_get_time();
    bool te_timeout = false;
    if (s_display.sync_to_te) {
        xSemaphoreTake(s_display.te, 0);
        te_timeout = !xSemaphoreTake(s_display.te, pdMS_TO_TICKS(40) + 1);
    }
    const int64_t transfer_start = esp_timer_get_time();
    const bool sent = send_dirty_area(frame, dirty, packed);
    const uint32_t transfer_us = esp_timer_get_time() - transfer_start;
    portENTER_CRITICAL(&s_stats_lock);
    s_display.stats.te_timeouts += te_timeout;
    s_display.stats.te_wait_us += transfer_start - wait_start;
    if (sent) {
        ++s_display.stats.frames;
        s_display.stats.prepare_us += prepare_us;
        s_display.stats.pixels += panel_width(dirty) * (dirty->y2 - dirty->y1 + 1);
        s_display.stats.transfer_us += transfer_us;
        s_display.stats.last_transfer_us = transfer_us;
        if (transfer_us > s_display.stats.max_transfer_us) {
            s_display.stats.max_transfer_us = transfer_us;
        }
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

#if !BSP_CONFIG_NO_GRAPHIC_LIB
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
        if (s_display.sleeping) {
            xSemaphoreGive(s_display.panel_idle);
            continue;
        }
        s_display.submitted_failed = !transfer_frame(s_display.scanout, &s_display.submitted,
                                                      s_display.submitted_prepare_us, false);
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
    if (s_display.sleeping) {
        lv_timer_pause(s_display.retry);
        return;
    }
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
    if (s_display.sleeping) {
        lv_display_flush_ready(display);
        return;
    }
    const int width = lv_area_get_width(area);
    const unsigned stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);
    const bool rotated = lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_0;
    lv_area_t target = *area;
    lv_display_rotate_area(display, &target);
    for (int y = target.y1; y <= target.y2; ++y) {
        if (s_display.stale_rows[y]) {
            if (target.x1 > s_display.submitted.x1 || target.x2 < s_display.submitted.x2) {
                sync_row(y);
            }
            s_display.stale_rows[y] = false;
        }
    }
    for (int y = area->y1; y <= area->y2; ++y) {
        const uint16_t *src = (const uint16_t *)(pixels + (y - area->y1) * stride);
        if (!rotated) {
            uint16_t *dst = s_display.frame + y * BSP_LCD_H_RES + area->x1;
            memcpy(dst, src, width * sizeof(*dst));
        } else {
            for (int x = area->x1; x <= area->x2; ++x) {
                lv_point_t point = {x, y};
                lv_display_rotate_point_ccw(display, &point);
                s_display.frame[point.y * BSP_LCD_H_RES + point.x] = src[x - area->x1];
            }
        }
    }
    add_dirty(&target);
    if (lv_display_flush_is_last(display)) {
        s_display.prepare_us = esp_timer_get_time() - s_display.refresh_start_us;
        if (s_display.panel_task) {
            submit_frame();
        } else if (transfer_frame(s_display.frame, &s_display.dirty, s_display.prepare_us, false)) {
            lv_timer_pause(s_display.retry);
            s_display.dirty_valid = false;
        } else {
            lv_timer_resume(s_display.retry);
        }
    }
    lv_display_flush_ready(display);
}

static void IRAM_ATTR touch_ready(esp_lcd_touch_handle_t touch)
{
    (void)touch;
    atomic_store_explicit(&s_touch_pending, true, memory_order_relaxed);
}

static void read_touch_report(void)
{
    if (s_display.sleeping) {
        s_display.point_count = 0;
        return;
    }
    /* A polled empty HYN212 frame is also decoded as zero contacts. Consume
     * reports only when INT announces data so an idle hold keeps its state. */
    if (!atomic_exchange_explicit(&s_touch_pending, false, memory_order_relaxed) &&
        gpio_get_level(CANDIS_TOUCH_INT)) {
        return;
    }
    esp_err_t err = esp_lcd_touch_read_data(s_display.touch);
    if (err == ESP_ERR_NOT_FINISHED) {
        return;
    }
    esp_lcd_touch_point_data_t points[TOUCH_POINTS];
    uint8_t count = 0;
    if (err == ESP_OK) {
        err = esp_lcd_touch_get_data(s_display.touch, points, &count, TOUCH_POINTS);
    }
    if (err != ESP_OK) {
        ++s_display.stats.touch_errors;
        if (++s_display.touch_read_failures < 3) {
            atomic_store_explicit(&s_touch_pending, true, memory_order_relaxed);
        } else {
            s_display.point_count = 0;
            s_display.touch_read_failures = 0;
        }
        return;
    }
    s_display.touch_read_failures = 0;
    for (uint8_t i = 0; i < count; ++i) {
        s_display.points[i] = (candis_touch_point_t){points[i].x, points[i].y, points[i].track_id};
    }
    s_display.point_count = count;
}

static void read_touch(lv_indev_t *input, lv_indev_data_t *data)
{
    if (s_display.sleeping) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = (lv_point_t){0, 0};
        return;
    }
    read_touch_report();
    data->state = s_display.point_count ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point = (lv_point_t){s_display.points[0].x, s_display.points[0].y};
}
#endif

static struct {
    candis_power_t power;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t touch;
    esp_err_t (*driver_delete)(esp_lcd_touch_handle_t);
} s_standalone_touch;

static esp_err_t create_touch(i2c_master_bus_handle_t bus, esp_lcd_touch_interrupt_callback_t callback,
                              const candis_touch_config_t *touch_config,
                              esp_lcd_panel_io_handle_t *handle, esp_lcd_touch_handle_t *touch)
{
    esp_lcd_panel_io_i2c_config_t io = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    io.dev_addr = CANDIS_TOUCH_ADDR;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io, handle), TAG, "init touch IO");
    const esp_lcd_touch_config_t config = {
        .x_max = BSP_LCD_H_RES, .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = CANDIS_TOUCH_RESET, .int_gpio_num = CANDIS_TOUCH_INT,
        .levels.interrupt = 0,
        .flags.swap_xy = touch_config ? touch_config->swap_xy : false,
        .flags.mirror_x = touch_config ? touch_config->mirror_x : true,
        .flags.mirror_y = touch_config ? touch_config->mirror_y : true,
        .interrupt_callback = callback,
    };
    return esp_lcd_touch_new_i2c_cst9220(*handle, &config, touch);
}

static esp_err_t release_touch(esp_lcd_touch_handle_t *touch, esp_lcd_panel_io_handle_t *io,
                               esp_err_t (*driver_delete)(esp_lcd_touch_handle_t))
{
    esp_err_t touch_err = ESP_OK;
    if (*touch) {
        if ((*touch)->config.interrupt_callback) {
            ESP_RETURN_ON_ERROR(gpio_isr_handler_remove((*touch)->config.int_gpio_num),
                                TAG, "remove touch interrupt handler");
            (*touch)->config.interrupt_callback = NULL;
        }
        /* With the ISR detached, CST9220 consumes the handle even on GPIO reset errors. */
        touch_err = driver_delete ? driver_delete(*touch) : esp_lcd_touch_del(*touch);
        *touch = NULL;
    }
    esp_err_t io_err = ESP_OK;
    if (*io) {
        io_err = esp_lcd_panel_io_del(*io);
        if (io_err == ESP_OK) {
            *io = NULL;
        }
    }
    return touch_err == ESP_OK ? io_err : touch_err;
}

static esp_err_t release_standalone_touch(void)
{
    const esp_err_t touch_err = release_touch(&s_standalone_touch.touch, &s_standalone_touch.io,
                                              s_standalone_touch.driver_delete);
    if (s_standalone_touch.touch || s_standalone_touch.io) {
        return touch_err;
    }
    s_standalone_touch.driver_delete = NULL;
    const esp_err_t power = candis_power_release(&s_standalone_touch.power);
    return touch_err == ESP_OK ? power : touch_err;
}

esp_err_t candis_touch_new_with_config(const candis_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    if (!ret_touch) {
        return ESP_ERR_INVALID_ARG;
    }
    *ret_touch = NULL;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!s_standalone_touch.power.bus
#if !BSP_CONFIG_NO_GRAPHIC_LIB
        && !s_display.touch_io
#endif
       ) {
        s_standalone_touch.power.rail = CANDIS_RAIL_DISPLAY;
        err = candis_power_acquire(&s_standalone_touch.power, config && config->manage_power, 150);
        if (err == ESP_OK) {
            err = create_touch(s_standalone_touch.power.bus, NULL, config, &s_standalone_touch.io,
                               &s_standalone_touch.touch);
        }
        if (err == ESP_OK) {
            s_standalone_touch.driver_delete = s_standalone_touch.touch->del;
            s_standalone_touch.touch->del = bsp_touch_del;
            *ret_touch = s_standalone_touch.touch;
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(release_standalone_touch());
        }
    }
    candis_control_leave();
    return err;
}

esp_err_t bsp_touch_new(const bsp_touch_config_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    (void)config;
    return candis_touch_new_with_config(NULL, ret_touch);
}

esp_err_t bsp_touch_del(esp_lcd_touch_handle_t touch)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = (!touch || touch == s_standalone_touch.touch) ? release_standalone_touch() : ESP_ERR_INVALID_ARG;
    candis_control_leave();
    return err;
}

esp_err_t candis_touch_read(esp_lcd_touch_handle_t touch, candis_touch_point_t points[2], uint8_t *count)
{
    if (!touch || !points || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (touch == s_standalone_touch.touch) {
        err = esp_lcd_touch_read_data(touch);
        if (err == ESP_OK) {
            esp_lcd_touch_point_data_t contacts[TOUCH_POINTS];
            uint8_t total;
            err = esp_lcd_touch_get_data(touch, contacts, &total, TOUCH_POINTS);
            if (err == ESP_OK) {
                for (uint8_t i = 0; i < total; ++i) {
                    points[i] = (candis_touch_point_t){contacts[i].x, contacts[i].y, contacts[i].track_id};
                }
                *count = total;
            }
        }
    }
    candis_control_leave();
    return err;
}

#if !BSP_CONFIG_NO_GRAPHIC_LIB
static void retry_refresh(lv_timer_t *timer)
{
    if (s_display.panel_task) {
        submit_frame();
        return;
    }
    lv_display_t *display = lv_timer_get_user_data(timer);
    lv_obj_invalidate(lv_display_get_screen_active(display));
}

static uint32_t ui_tick_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool ui_lock(uint32_t timeout_ms)
{
    const SemaphoreHandle_t lock = atomic_load(&s_ui_lock);
    return lock && xSemaphoreTakeRecursive(lock, timeout_ms ? pdMS_TO_TICKS(timeout_ms) + 1 : portMAX_DELAY) == pdTRUE;
}

static void ui_unlock(void)
{
    xSemaphoreGiveRecursive(atomic_load(&s_ui_lock));
}

static bool ui_held_by_caller(void)
{
    const SemaphoreHandle_t lock = atomic_load(&s_ui_lock);
    return lock && xSemaphoreGetMutexHolder(lock) == xTaskGetCurrentTaskHandle();
}

static void ui_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ui_lock(0);
        if (!atomic_load(&s_active)) {
            SemaphoreHandle_t done = s_display.ui_done;
            ui_unlock();
            xSemaphoreGive(done);
            vTaskDeleteWithCaps(NULL);
            return;
        }
        uint32_t delay = lv_timer_handler();
        if (delay > s_display.max_sleep_ms) {
            delay = s_display.max_sleep_ms;
        }
        ui_unlock();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay) + 1);
    }
}

static esp_err_t ui_create(const candis_display_config_t *config, const lvgl_port_cfg_t *port)
{
    if (!atomic_load(&s_ui_lock)) {
        atomic_store(&s_ui_lock, xSemaphoreCreateRecursiveMutexStatic(&s_ui_lock_storage));
    }
    ESP_RETURN_ON_FALSE(ui_lock(2000), ESP_ERR_TIMEOUT, TAG, "lock LVGL for startup");
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (lv_is_initialized()) {
        goto out;
    }
    lv_init();
    s_display.lvgl_initialized = true;
    lv_tick_set_cb(ui_tick_ms);
    err = ESP_ERR_NO_MEM;
    s_display.display = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (!s_display.display) {
        goto out;
    }
    lv_display_set_color_format(s_display.display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display.display, s_display.draw, s_display.draw_second, s_display.draw_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(s_display.display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(s_display.display, mark_refresh_start, LV_EVENT_REFR_START, NULL);
    lv_display_set_flush_cb(s_display.display, flush_display);
    lv_obj_t *screen = lv_display_get_screen_active(s_display.display);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    s_display.retry = lv_timer_create(retry_refresh, s_display.panel_task ? 5 : 100, s_display.display);
    if (!s_display.retry) {
        goto out;
    }
    lv_timer_pause(s_display.retry);
    if (config->enable_touch) {
        s_display.input = lv_indev_create();
        if (!s_display.input) {
            goto out;
        }
        lv_indev_set_type(s_display.input, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(s_display.input, s_display.display);
        lv_indev_set_read_cb(s_display.input, read_touch);
    }
    s_display.ui_done = xSemaphoreCreateBinary();
    if (!s_display.ui_done) {
        goto out;
    }
    s_display.max_sleep_ms = port->task_max_sleep_ms ? port->task_max_sleep_ms : 500;
    const unsigned caps = port->task_stack_caps ? port->task_stack_caps : MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT;
    if (xTaskCreatePinnedToCoreWithCaps(ui_worker, "candis_lvgl", port->task_stack, NULL, port->task_priority,
                                        &s_display.ui_task, port->task_affinity < 0 ? tskNO_AFFINITY : port->task_affinity,
                                        caps) == pdPASS) {
        atomic_store(&s_active, true);
        err = ESP_OK;
    }
out:
    ui_unlock();
    return err;
}

static esp_err_t release_ui(void)
{
    if (s_display.ui_task) {
        ESP_RETURN_ON_FALSE(xTaskGetCurrentTaskHandle() != s_display.ui_task, ESP_ERR_INVALID_STATE,
                            TAG, "stop display from a control task");
        if (atomic_load(&s_active)) {
            ESP_RETURN_ON_FALSE(ui_lock(2000), ESP_ERR_TIMEOUT, TAG, "lock LVGL for shutdown");
            atomic_store(&s_active, false);
            xTaskNotifyGive(s_display.ui_task);
            ui_unlock();
        }
        ESP_RETURN_ON_FALSE(xSemaphoreTake(s_display.ui_done, pdMS_TO_TICKS(2000) + 1) == pdTRUE,
                            ESP_ERR_TIMEOUT, TAG, "wait for LVGL task exit");
        s_display.ui_task = NULL;
    }
    if (s_display.lvgl_initialized) {
        ESP_RETURN_ON_FALSE(ui_lock(2000), ESP_ERR_TIMEOUT, TAG, "lock LVGL for release");
        if (s_display.input) {
            lv_indev_delete(s_display.input);
            s_display.input = NULL;
        }
        if (s_display.retry) {
            lv_timer_delete(s_display.retry);
            s_display.retry = NULL;
        }
        if (s_display.display) {
            lv_display_delete(s_display.display);
            s_display.display = NULL;
        }
        lv_deinit();
        s_display.lvgl_initialized = false;
        ui_unlock();
    }
    if (s_display.ui_done) {
        vSemaphoreDelete(s_display.ui_done);
        s_display.ui_done = NULL;
    }
    return ESP_OK;
}
#endif

static esp_err_t release_display(void)
{
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    ESP_RETURN_ON_FALSE(!ui_held_by_caller(), ESP_ERR_INVALID_STATE, TAG, "release outside the LVGL lock");
#endif
    atomic_store(&s_raw_active, false);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    ESP_RETURN_ON_ERROR(release_ui(), TAG, "release display UI");
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
#endif
    ESP_RETURN_ON_FALSE(drain_dma(), ESP_ERR_TIMEOUT, TAG, "panel DMA still pending");
    if (s_display.te_handler) {
        ESP_RETURN_ON_ERROR(gpio_isr_handler_remove(CANDIS_LCD_TE), TAG, "remove TE handler");
        s_display.te_handler = false;
    }
    if (s_display.te_pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(CANDIS_LCD_TE), TAG, "release TE pin");
        s_display.te_pin = false;
    }
    esp_err_t sleep = ESP_OK;
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    ESP_RETURN_ON_ERROR(release_touch(&s_display.touch, &s_display.touch_io, NULL), TAG, "release display touch");
#endif
    if (s_display.panel_awake && (!s_display.sleeping || s_display.sleep_pending)) {
        sleep = panel_command(LCD_CMD_DISPOFF, NULL, 0);
        if (sleep == ESP_OK) {
            sleep = panel_command(LCD_CMD_SLPIN, NULL, 0);
        }
        if (sleep == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(120) + 1);
        } else {
            ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_LCD_RESET, 0), TAG, "hold panel in reset");
        }
        s_display.panel_awake = false;
    }
    if (s_display.panel_io) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_del(s_display.panel_io), TAG, "release panel IO");
        s_display.panel_io = NULL;
    }
    if (s_display.spi) {
        ESP_RETURN_ON_ERROR(spi_bus_free(CANDIS_LCD_SPI), TAG, "release QSPI");
        s_display.spi = false;
    }
    if (s_display.reset_pin) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(CANDIS_LCD_RESET), TAG, "release reset pin");
        s_display.reset_pin = false;
    }
    ESP_RETURN_ON_ERROR(candis_power_release(&s_display.power), TAG, "release panel rail");
    if (s_display.te) {
        vSemaphoreDelete(s_display.te);
    }
    if (s_display.dma) {
        vSemaphoreDelete(s_display.dma);
    }
    heap_caps_free(s_display.transfer[0]);
    heap_caps_free(s_display.transfer[1]);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (s_display.panel_idle) {
        vSemaphoreDelete(s_display.panel_idle);
    }
    heap_caps_free(s_display.scanout);
    heap_caps_free(s_display.draw_second);
    heap_caps_free(s_display.draw);
    heap_caps_free(s_display.frame);
#endif
    memset(&s_display, 0, sizeof(s_display));
    return sleep;
}

static esp_err_t start_panel(const candis_display_config_t *config)
{
    s_display.sync_to_te = config->sync_to_te;
    s_display.brightness = config->brightness_percent;
    s_display.last_nonzero_brightness = config->brightness_percent ? config->brightness_percent : 50;
    atomic_store(&s_te_edges, 0);
    s_display.transfer[0] = heap_caps_aligned_alloc(64, DRAW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_display.transfer[1] = heap_caps_aligned_alloc(64, DRAW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    s_display.te = xSemaphoreCreateBinary();
    s_display.dma = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_display.transfer[0] && s_display.transfer[1] &&
                        s_display.te && s_display.dma, ESP_ERR_NO_MEM, TAG, "display buffers or semaphores");
    s_display.power.rail = CANDIS_RAIL_DISPLAY;
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_display.power, config->manage_panel_power, 150),
                        TAG, "prepare panel rail");
    return panel_init(config);
}

static void rollback_display(void)
{
    const esp_err_t cleanup = release_display();
    if (cleanup != ESP_OK) {
        ESP_LOGE(TAG, "startup cleanup: %s; retry candis_display_stop", esp_err_to_name(cleanup));
    }
}

static bool panel_claimed(void)
{
    return s_display.te || s_display.dma || s_display.power.bus;
}

static esp_err_t open_raw_display(const candis_display_config_t *config, esp_lcd_panel_io_handle_t *io)
{
    if (!config || config->brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (!panel_claimed()) {
        s_display.exposed_panel = io != NULL;
        err = start_panel(config);
        if (err == ESP_OK) {
            atomic_store(&s_raw_active, true);
            if (io) {
                *io = s_display.panel_io;
            }
        } else {
            rollback_display();
        }
    }
    candis_control_leave();
    return err;
}

#if !BSP_CONFIG_NO_GRAPHIC_LIB
static esp_err_t start_display(const candis_display_config_t *config, const bsp_display_cfg_t *ui)
{
    s_display.frame = heap_caps_calloc(FRAME_PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_display.draw_bytes = ui->buffer_size ? ui->buffer_size * sizeof(uint16_t) : DRAW_BYTES;
    unsigned caps = MALLOC_CAP_8BIT | (ui->flags.buff_spiram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
    if (ui->flags.buff_dma) {
        caps |= MALLOC_CAP_DMA;
    }
    s_display.draw = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, s_display.draw_bytes, caps);
    if (ui->double_buffer) {
        s_display.draw_second = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, s_display.draw_bytes, caps);
    }
    ESP_RETURN_ON_FALSE(s_display.frame && s_display.draw && (!ui->double_buffer || s_display.draw_second),
                        ESP_ERR_NO_MEM, TAG, "LVGL buffers");
    ESP_RETURN_ON_ERROR(start_panel(config), TAG, "start panel");
    if (config->enable_touch) {
        atomic_store_explicit(&s_touch_pending, false, memory_order_relaxed);
        ESP_RETURN_ON_ERROR(create_touch(s_display.power.bus, touch_ready, NULL, &s_display.touch_io, &s_display.touch),
                            TAG, "start touch");
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
    return ui_create(config, &ui->lvgl_port_cfg);
}

static esp_err_t open_display(const candis_display_config_t *config, const bsp_display_cfg_t *ui,
                              lv_display_t **display)
{
    if (!config || config->brightness_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    *display = NULL;
    const unsigned min_buffer = 2 * LV_ROUND_UP(BSP_LCD_H_RES * sizeof(uint16_t), LV_DRAW_BUF_STRIDE_ALIGN) /
                                sizeof(uint16_t);
    if (!ui || ui->buffer_size > FRAME_PIXELS ||
        (ui->buffer_size && ui->buffer_size < min_buffer) ||
        (ui->flags.buff_dma && ui->flags.buff_spiram) ||
        ui->lvgl_port_cfg.task_affinity < -1 || ui->lvgl_port_cfg.task_affinity >= configNUMBER_OF_CORES ||
        ui->lvgl_port_cfg.task_stack <= 0 || ui->lvgl_port_cfg.task_priority < 0 ||
        ui->lvgl_port_cfg.task_priority >= configMAX_PRIORITIES || ui->lvgl_port_cfg.task_max_sleep_ms < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (panel_claimed() || s_display.frame || (s_standalone_touch.power.bus && config->enable_touch)) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_display(config, ui);
    if (err == ESP_OK) {
        *display = s_display.display;
    } else {
        rollback_display();
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_display_start(const candis_display_config_t *config, lv_display_t **display)
{
    bsp_display_cfg_t ui = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    };
    ui.lvgl_port_cfg.task_stack = 6144;
    ui.lvgl_port_cfg.task_affinity = 0;
    return display ? open_display(config, &ui, display) : ESP_ERR_INVALID_ARG;
}
#endif

esp_err_t candis_display_start_raw(const candis_display_config_t *config)
{
    return open_raw_display(config, NULL);
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
    if (atomic_load(&s_raw_active) && !s_display.exposed_panel && !s_display.sleeping) {
        const panel_area_t full = {.x2 = BSP_LCD_H_RES - 1, .y2 = BSP_LCD_V_RES - 1};
        err = transfer_frame(pixels, &full, 0, false) ? ESP_OK : ESP_FAIL;
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

#if !BSP_CONFIG_NO_GRAPHIC_LIB
bool bsp_display_lock(uint32_t timeout_ms)
{
    if (xPortInIsrContext() || !atomic_load(&s_active) || !ui_lock(timeout_ms)) {
        return false;
    }
    if (!atomic_load(&s_active)) {
        ui_unlock();
        return false;
    }
    return true;
}

void bsp_display_unlock(void)
{
    if (!xPortInIsrContext() && ui_held_by_caller()) {
        ui_unlock();
    }
}

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t ui = {.lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG()};
    ui.lvgl_port_cfg.task_stack = 6144;
    ui.lvgl_port_cfg.task_affinity = 0;
    return bsp_display_start_with_config(&ui);
}

lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *ui)
{
    candis_display_config_t config = CANDIS_DISPLAY_CONFIG_DEFAULT();
    config.brightness_percent = 0;
    lv_display_t *display = NULL;
    ESP_ERROR_CHECK_WITHOUT_ABORT(open_display(&config, ui, &display));
    return display;
}

void bsp_display_stop(lv_display_t *display)
{
    if (!display || !candis_control_enter()) {
        return;
    }
    if (display == s_display.display) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_display());
    }
    candis_control_leave();
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    if (!bsp_display_lock(2000)) {
        return NULL;
    }
    lv_indev_t *input = s_display.input;
    bsp_display_unlock();
    return input;
}

void bsp_display_rotate(lv_display_t *display, lv_display_rotation_t rotation)
{
    if ((unsigned)rotation > LV_DISPLAY_ROTATION_270 || !bsp_display_lock(2000)) {
        return;
    }
    if (display && display == s_display.display) {
        lv_display_set_rotation(display, rotation);
        lv_obj_invalidate(lv_display_get_screen_active(display));
    }
    bsp_display_unlock();
}
#endif

static esp_err_t raw_draw(esp_lcd_panel_t *panel, int x1, int y1, int x2, int y2, const void *pixels)
{
    (void)panel;
    if (!pixels || x1 < 0 || y1 < 0 || x2 <= x1 || y2 <= y1 ||
        x2 > BSP_LCD_H_RES || y2 > BSP_LCD_V_RES || ((x1 | y1 | x2 | y2) & 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_display.exposed_panel && atomic_load(&s_raw_active) && !s_display.sleeping) {
        const panel_area_t window = {x1, y1, x2 - 1, y2 - 1};
        err = transfer_frame(pixels, &window, 0, true) ? ESP_OK : ESP_FAIL;
    }
    candis_control_leave();
    return err;
}

static esp_err_t raw_panel_ready(esp_lcd_panel_t *panel)
{
    (void)panel;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_display.exposed_panel && atomic_load(&s_raw_active) ? ESP_OK : ESP_ERR_INVALID_STATE;
    candis_control_leave();
    return err;
}

static esp_err_t raw_panel_delete(esp_lcd_panel_t *panel)
{
    (void)panel;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_display.exposed_panel ? release_display() : ESP_ERR_INVALID_STATE;
    candis_control_leave();
    return err;
}

static esp_err_t raw_panel_on_off(esp_lcd_panel_t *panel, bool on)
{
    (void)panel;
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_display.exposed_panel && atomic_load(&s_raw_active) && !s_display.sleeping) {
        err = drain_dma() ? panel_command(on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0) : ESP_ERR_TIMEOUT;
        if (err == ESP_OK) {
            s_display.panel_visible = on;
        }
    }
    candis_control_leave();
    return err;
}

static esp_lcd_panel_t s_raw_panel = {
    .init = raw_panel_ready,
    .del = raw_panel_delete,
    .draw_bitmap = raw_draw,
    .disp_on_off = raw_panel_on_off,
};

static esp_err_t wait_transfer_idle(uint32_t timeout_ms)
{
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (s_display.panel_idle) {
        if (!xSemaphoreTake(s_display.panel_idle, pdMS_TO_TICKS(timeout_ms) + 1)) {
            return ESP_ERR_TIMEOUT;
        }
        xSemaphoreGive(s_display.panel_idle);
    }
#else
    (void)timeout_ms;
#endif
    return drain_dma() ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t panel_sleep_transition(bool sleep)
{
    if (sleep) {
#if !BSP_CONFIG_NO_GRAPHIC_LIB
        if (s_display.touch) {
            ESP_RETURN_ON_ERROR(esp_lcd_touch_enter_sleep(s_display.touch), TAG, "sleep CST9220");
        }
#endif
        ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_DISPOFF, NULL, 0), TAG, "DISPOFF");
        ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_SLPIN, NULL, 0), TAG, "SLPIN");
        vTaskDelay(pdMS_TO_TICKS(120) + 1);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(panel_command(LCD_CMD_SLPOUT, NULL, 0), TAG, "SLPOUT");
    vTaskDelay(pdMS_TO_TICKS(120) + 1);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (s_display.touch) {
        ESP_RETURN_ON_ERROR(esp_lcd_touch_exit_sleep(s_display.touch), TAG, "wake CST9220");
    }
#endif
    ESP_RETURN_ON_ERROR(panel_brightness(s_display.brightness), TAG, "restore brightness");
    return panel_command(s_display.panel_visible ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}

static esp_err_t set_display_sleep_locked(bool sleep)
{
    bool active = atomic_load(&s_raw_active);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    const bool lvgl = atomic_load(&s_active);
    active |= lvgl;
    if (ui_held_by_caller()) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    if (!active || !s_display.panel_awake) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_display.sleeping == sleep && !s_display.sleep_pending) {
        return ESP_OK;
    }
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (lvgl && !ui_lock(2000)) {
        return ESP_ERR_TIMEOUT;
    }
#endif
    /* Holding LVGL prevents new submissions while the panel worker finishes. */
    esp_err_t err = wait_transfer_idle(2000);
    if (err == ESP_OK) {
        s_display.sleeping = true;
        s_display.sleep_pending = true;
#if !BSP_CONFIG_NO_GRAPHIC_LIB
        if (lvgl) {
            lv_timer_pause(s_display.retry);
            s_display.point_count = 0;
        }
#endif
        err = panel_sleep_transition(sleep);
        if (err == ESP_OK) {
            s_display.sleeping = sleep;
            s_display.sleep_pending = false;
        }
    }
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (lvgl) {
        if (err == ESP_OK && !s_display.sleeping) {
            lv_obj_invalidate(lv_display_get_screen_active(s_display.display));
            lv_timer_resume(s_display.retry);
        }
        ui_unlock();
    }
#endif
    return err;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    (void)config;
    if (!ret_panel || !ret_io) {
        return ESP_ERR_INVALID_ARG;
    }
    *ret_panel = NULL;
    *ret_io = NULL;
    candis_display_config_t panel = CANDIS_DISPLAY_CONFIG_DEFAULT();
    panel.brightness_percent = 0;
    const esp_err_t err = open_raw_display(&panel, ret_io);
    if (err == ESP_OK) {
        *ret_panel = &s_raw_panel;
    }
    return err;
}

esp_err_t bsp_display_new_with_handles(const bsp_display_config_t *config, bsp_lcd_handles_t *ret_handles)
{
    if (!ret_handles) {
        return ESP_ERR_INVALID_ARG;
    }
    *ret_handles = (bsp_lcd_handles_t){0};
    esp_err_t err = bsp_display_new(config, &ret_handles->panel, &ret_handles->io);
    if (err == ESP_OK) {
        ret_handles->control = NULL;
    }
    return err;
}

void bsp_display_delete(void)
{
    if (!candis_control_enter()) {
        return;
    }
    if (s_display.exposed_panel) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_display());
    }
    candis_control_leave();
}

esp_err_t bsp_display_enter_sleep(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = set_display_sleep_locked(true);
    candis_control_leave();
    return err;
}

esp_err_t bsp_display_exit_sleep(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = set_display_sleep_locked(false);
    candis_control_leave();
    return err;
}

static esp_err_t display_brightness(int percent, bool allow_closed)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    bool active = atomic_load(&s_raw_active);
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    const bool lvgl = atomic_load(&s_active);
    active |= lvgl;
    if (lvgl && !ui_lock(2000)) {
        candis_control_leave();
        return ESP_ERR_TIMEOUT;
    }
#endif
    esp_err_t err = allow_closed ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (active) {
        const unsigned brightness = percent < 0 ? s_display.last_nonzero_brightness : (unsigned)percent;
        err = ESP_OK;
        if (!s_display.sleeping) {
            err = wait_transfer_idle(200);
            if (err == ESP_OK) {
                err = panel_brightness(brightness);
            }
        }
        if (err == ESP_OK) {
            s_display.brightness = brightness;
            if (brightness) {
                s_display.last_nonzero_brightness = brightness;
            }
        }
    }
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    if (lvgl) {
        ui_unlock();
    }
#endif
    candis_control_leave();
    return err;
}

esp_err_t bsp_display_brightness_init(void)
{
    return ESP_OK;
}

esp_err_t bsp_display_brightness_deinit(void)
{
    return display_brightness(0, true);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    return brightness_percent < 0 || brightness_percent > 100 ? ESP_ERR_INVALID_ARG
                                                               : display_brightness(brightness_percent, false);
}

esp_err_t bsp_display_backlight_on(void)
{
    return display_brightness(-1, false);
}

esp_err_t bsp_display_backlight_off(void)
{
    return display_brightness(0, false);
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
    } else {
#if !BSP_CONFIG_NO_GRAPHIC_LIB
        if (!bsp_display_lock(2000)) {
            return ESP_ERR_INVALID_STATE;
        }
#else
        return ESP_ERR_INVALID_STATE;
#endif
    }
    portENTER_CRITICAL(&s_stats_lock);
    *stats = s_display.stats;
    portEXIT_CRITICAL(&s_stats_lock);
    stats->te_edges = atomic_load_explicit(&s_te_edges, memory_order_relaxed);
    if (raw) {
        candis_control_leave();
#if !BSP_CONFIG_NO_GRAPHIC_LIB
    } else {
        bsp_display_unlock();
#endif
    }
    return ESP_OK;
}

#if !BSP_CONFIG_NO_GRAPHIC_LIB
uint8_t candis_display_get_touch_points(candis_touch_point_t points[2])
{
    if (!points) {
        return 0;
    }
    memcpy(points, s_display.points, s_display.point_count * sizeof(*points));
    return s_display.point_count;
}
#endif
