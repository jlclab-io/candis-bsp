# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


STREAM = Path(__file__).resolve().parents[1] / "components/candis-camera/src/candis_camera_stream.c"

STUB = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT };
enum v4l2_buf_type { CAPTURE = 1 };
enum { VIDIOC_STREAMOFF = 2 };
#define BUFFER_COUNT 2
#define TAG "camera_stop_test"
#define ESP_LOGE(tag, ...) ((void)(tag))

static int s_fd = 10;
static bool s_streaming = true;
static bool s_borrowed = true;
static struct { void *address; size_t length; } s_buffers[BUFFER_COUNT];
static esp_err_t stop_result;
static unsigned stop_calls;
static unsigned unmap_calls;
static unsigned close_calls;
static unsigned fail_unmap_call;

static esp_err_t video_ioctl(unsigned long command, void *arg, const char *operation)
{
    assert(command == VIDIOC_STREAMOFF);
    assert(arg && *(int *)arg == CAPTURE);
    assert(operation);
    ++stop_calls;
    return stop_result;
}

static int munmap(void *address, size_t length)
{
    assert(address && length);
    assert(!s_streaming && !s_borrowed && stop_calls == 1);
    ++unmap_calls;
    if (unmap_calls == fail_unmap_call) {
        fail_unmap_call = 0;
        return -1;
    }
    return 0;
}

static int close(int fd)
{
    assert(fd == 10);
    ++close_calls;
    return 0;
}
"""

TEST = r"""
static void reset_state(void)
{
    static uint8_t frames[BUFFER_COUNT][16];
    s_fd = 10;
    s_streaming = true;
    s_borrowed = true;
    for (unsigned i = 0; i < BUFFER_COUNT; ++i) {
        s_buffers[i].address = frames[i];
        s_buffers[i].length = sizeof(frames[i]);
    }
    stop_result = ESP_OK;
    stop_calls = 0;
    unmap_calls = 0;
    close_calls = 0;
    fail_unmap_call = 0;
}

int main(void)
{
    reset_state();
    assert(candis_camera_stream_stop() == ESP_OK);
    assert(stop_calls == 1 && !s_streaming && !s_borrowed);
    assert(!s_buffers[0].length && !s_buffers[1].length && s_fd == -1);
    assert(unmap_calls == 2 && close_calls == 1);

    reset_state();
    stop_result = ESP_ERR_TIMEOUT;
    assert(candis_camera_stream_stop() == ESP_ERR_TIMEOUT);
    assert(stop_calls == 1 && s_streaming && s_borrowed);
    assert(s_buffers[0].length && s_buffers[1].length && s_fd == 10);
    assert(!unmap_calls && !close_calls);

    reset_state();
    fail_unmap_call = 2;
    assert(candis_camera_stream_stop() == ESP_FAIL);
    assert(!s_streaming && !s_borrowed && !s_buffers[0].length && s_buffers[1].length);
    assert(s_fd == 10 && unmap_calls == 2 && !close_calls);
    assert(candis_camera_stream_stop() == ESP_OK);
    assert(!s_buffers[1].length && s_fd == -1 && unmap_calls == 3 && close_calls == 1);
}
"""


class CameraStopTest(unittest.TestCase):
    def test_streamoff_is_attempted_before_releasing_buffers_and_retries(self):
        source = STREAM.read_text()
        start = source.index("esp_err_t candis_camera_stream_stop(void)")
        end = source.index("bool candis_camera_stream_is_open(void)", start)
        with TemporaryDirectory() as directory:
            root = Path(directory)
            test = root / "camera_stop.c"
            test.write_text(STUB + source[start:end] + TEST)
            binary = root / "camera_stop"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
