# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


DISPLAY = Path(__file__).resolve().parents[1] / "components/candis/src/candis_display.c"

STUBS = r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define IRAM_ATTR
#define TOUCH_POINTS 2
#define CANDIS_TOUCH_INT 1
enum { ESP_OK, ESP_ERR_NOT_FINISHED, ESP_ERR_TIMEOUT };

typedef int esp_err_t;
typedef void *esp_lcd_touch_handle_t;
typedef struct { uint16_t x, y; uint8_t track_id; } candis_touch_point_t;
typedef candis_touch_point_t esp_lcd_touch_point_data_t;

static atomic_bool s_touch_pending;
static struct {
    esp_lcd_touch_handle_t touch;
    bool sleeping;
    uint8_t point_count;
    uint8_t touch_read_failures;
    candis_touch_point_t points[TOUCH_POINTS];
    struct { unsigned touch_errors; } stats;
} s_display;

static esp_err_t next_result;
static unsigned read_calls;
static int pin_level = 1;
static uint8_t next_points;
static const esp_lcd_touch_point_data_t positions[] = {{10, 20, 1}, {30, 40, 2}};

static int gpio_get_level(int pin)
{
    assert(pin == CANDIS_TOUCH_INT);
    return pin_level;
}

static esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t touch)
{
    assert(touch == s_display.touch);
    ++read_calls;
    return next_result;
}

static esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t touch, esp_lcd_touch_point_data_t *points,
                                       uint8_t *count, unsigned capacity)
{
    assert(touch == s_display.touch && capacity == TOUCH_POINTS);
    *count = next_points;
    memcpy(points, positions, next_points * sizeof(*points));
    return ESP_OK;
}
"""

TEST = r"""
int main(void)
{
    next_points = 2;
    touch_ready(s_display.touch);
    read_touch_report();
    assert(s_display.point_count == 2);
    assert(s_display.points[0].track_id == 1 && s_display.points[1].track_id == 2);
    next_points = 0;
    for (int idle = 0; idle < 500; ++idle) {
        read_touch_report();
    }
    assert(s_display.point_count == 2 && read_calls == 1);

    next_result = ESP_ERR_NOT_FINISHED;
    touch_ready(s_display.touch);
    read_touch_report();
    assert(s_display.point_count == 2 && read_calls == 2 && s_display.stats.touch_errors == 0);

    next_result = ESP_ERR_TIMEOUT;
    touch_ready(s_display.touch);
    read_touch_report();
    read_touch_report();
    assert(s_display.point_count == 2 && s_display.stats.touch_errors == 2);
    next_result = ESP_OK;
    read_touch_report();
    assert(s_display.point_count == 0 && s_display.touch_read_failures == 0);

    next_points = 1;
    pin_level = 0;
    read_touch_report();
    assert(s_display.point_count == 1);
    pin_level = 1;
    next_result = ESP_ERR_TIMEOUT;
    touch_ready(s_display.touch);
    read_touch_report();
    read_touch_report();
    read_touch_report();
    assert(s_display.point_count == 0 && s_display.stats.touch_errors == 5);
}
"""


class TouchReportTest(unittest.TestCase):
    def test_idle_hold_release_and_read_failures(self):
        source = DISPLAY.read_text()
        start = source.index("static void IRAM_ATTR touch_ready(")
        end = source.index("static void read_touch(", start)
        with TemporaryDirectory() as directory:
            work = Path(directory)
            harness = work / "touch_test.c"
            harness.write_text(STUBS + source[start:end] + TEST)
            executable = work / "touch_test"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(harness), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
