# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


DISPLAY = Path(__file__).resolve().parents[1] / "components/candis/src/candis_display.c"

STUB = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BSP_CONFIG_NO_GRAPHIC_LIB 0
typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
enum { LCD_CMD_DISPOFF, LCD_CMD_SLPIN, LCD_CMD_SLPOUT, LCD_CMD_DISPON };
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); \
    const esp_err_t error = (call); if (error != ESP_OK) return error; } while (0)

static const char *TAG = "display_test";
static atomic_bool s_raw_active, s_active = true;
static bool locked, idle = true, transfer_joined, dma_idle = true, caller_holds_lvgl, touch_awake = true;
static int command_fail = -1;
static unsigned observed_brightness, commands, invalidations;
static struct {
    void *panel_idle, *touch, *retry, *display;
    bool panel_awake, panel_visible, sleeping, sleep_pending;
    unsigned brightness, point_count;
} s_display = {.panel_awake = true, .panel_visible = true, .panel_idle = &idle, .touch = &touch_awake};

static bool ui_held_by_caller(void) { return caller_holds_lvgl; }
static bool ui_lock(unsigned ms) { assert(ms == 2000 && !locked); return locked = true; }
static void ui_unlock(void) { assert(locked); locked = false; transfer_joined = false; }

static bool xSemaphoreTake(void *sem, unsigned ticks)
{
    assert(locked && sem == &idle && ticks == 2001);
    return idle;
}

static void xSemaphoreGive(void *sem)
{
    assert(sem == &idle);
    transfer_joined = true;
}

static bool drain_dma(void)
{
    assert(locked && transfer_joined);
    return dma_idle;
}

static esp_err_t esp_lcd_touch_enter_sleep(void *touch)
{
    assert(touch == &touch_awake && locked && transfer_joined && dma_idle);
    touch_awake = false;
    return ESP_OK;
}

static esp_err_t esp_lcd_touch_exit_sleep(void *touch)
{
    assert(touch == &touch_awake && locked && transfer_joined && dma_idle);
    touch_awake = true;
    return ESP_OK;
}

static esp_err_t panel_command(int command, const void *data, size_t len)
{
    assert(locked && transfer_joined && dma_idle && data == NULL && len == 0);
    ++commands;
    return command == command_fail ? ESP_FAIL : ESP_OK;
}

static esp_err_t panel_brightness(unsigned brightness)
{
    assert(locked && touch_awake);
    observed_brightness = brightness;
    return ESP_OK;
}

static void vTaskDelay(unsigned ticks) { assert(locked && ticks == 121); }
static void lv_timer_pause(void *timer) { assert(locked); (void)timer; }
static void lv_timer_resume(void *timer) { assert(locked); (void)timer; }
static void *lv_display_get_screen_active(void *display) { return display; }
static void lv_obj_invalidate(void *obj) { assert(locked); (void)obj; ++invalidations; }
"""

TEST = r"""
int main(void)
{
    caller_holds_lvgl = true;
    assert(set_display_sleep_locked(true) == ESP_ERR_INVALID_STATE && !commands);
    caller_holds_lvgl = false;
    idle = false;
    assert(set_display_sleep_locked(true) == ESP_ERR_TIMEOUT && !commands && !s_display.sleeping);
    idle = true;
    dma_idle = false;
    assert(set_display_sleep_locked(true) == ESP_ERR_TIMEOUT && !commands && touch_awake);

    dma_idle = true;
    command_fail = LCD_CMD_SLPIN;
    assert(set_display_sleep_locked(true) == ESP_FAIL);
    assert(s_display.sleeping && s_display.sleep_pending && !touch_awake && !invalidations);
    command_fail = -1;
    assert(set_display_sleep_locked(true) == ESP_OK && !s_display.sleep_pending);
    const unsigned asleep_commands = commands;
    assert(set_display_sleep_locked(true) == ESP_OK && commands == asleep_commands);
    observed_brightness = 100;
    s_display.brightness = 0;
    command_fail = LCD_CMD_DISPON;
    assert(set_display_sleep_locked(false) == ESP_FAIL && s_display.sleeping && s_display.sleep_pending);
    assert(!observed_brightness && !invalidations);
    command_fail = -1;
    assert(set_display_sleep_locked(false) == ESP_OK && !s_display.sleeping && !s_display.sleep_pending);
    assert(touch_awake && !observed_brightness && invalidations == 1);
}
"""


class DisplayLifecycleTest(unittest.TestCase):
    def test_dma_join_and_partial_sleep_recovery(self):
        source = DISPLAY.read_text()
        start = source.index("static esp_err_t wait_transfer_idle(")
        end = source.index("esp_err_t bsp_display_new(", start)
        with TemporaryDirectory() as directory:
            root = Path(directory)
            test = root / "display_lifecycle.c"
            test.write_text(STUB + source[start:end] + TEST)
            binary = root / "display_lifecycle"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
