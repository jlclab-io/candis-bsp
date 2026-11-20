# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


STUB = r"""
#pragma once
#include <stdlib.h>
#include "esp_err.h"
#define ESP_RETURN_ON_ERROR(expression, ...) do {                \
    esp_err_t result = (expression);                              \
    if (result != ESP_OK) return result;                           \
} while (0)
#define ESP_RETURN_ON_FALSE(condition, error, ...) do {            \
    if (!(condition)) return (error);                             \
} while (0)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define heap_caps_malloc(size, flags) malloc(size)
#define heap_caps_free free
#define pdTRUE 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(ms) (ms)
typedef void *QueueHandle_t;
static inline QueueHandle_t xQueueCreate(int count, size_t size)
{
    static int queue;
    (void)count;
    (void)size;
    return &queue;
}
static inline int queue_call(QueueHandle_t queue, const void *data, int ticks)
{
    (void)queue;
    (void)data;
    (void)ticks;
    return pdTRUE;
}
#define xQueueOverwrite(queue, data) queue_call(queue, data, 0)
#define xQueuePeek queue_call
#define xQueueReceive queue_call
#define xQueueSend queue_call
#define vQueueDelete(queue) ((void)(queue))
static inline void vTaskDelay(int ticks) { (void)ticks; }
"""

SOURCE = r"""
#include <assert.h>
#include "recorder.c"

static unsigned reads, writes, opens, closes;
static unsigned cancel_read, short_read, cancel_write, short_write;
static unsigned applied_volume, volume_changes;
static bool exercise_volume;
static bool amp, fail_close;
static int16_t first, last;
static recorder_route_t playback_route;

esp_err_t candis_audio_open(bool manage)
{
    assert(!manage && !amp);
    ++opens;
    return ESP_OK;
}

esp_err_t candis_audio_close(void)
{
    ++closes;
    amp = false;
    return fail_close ? ESP_FAIL : ESP_OK;
}

esp_err_t candis_audio_read(int16_t *pcm, size_t frames, size_t *received, uint32_t ms)
{
    assert(!amp && frames == BLOCK_FRAMES && ms == IO_TIMEOUT_MS);
    ++reads;
    *received = reads == short_read ? frames - 1 : frames;
    for (size_t frame = 0; frame < *received; ++frame) {
        pcm[2 * frame] = INT16_MIN;
        pcm[2 * frame + 1] = 900;
    }
    if (reads == cancel_read) {
        recorder_stop();
    }
    return ESP_OK;
}

esp_err_t candis_audio_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t ms)
{
    assert(!amp && frames == BLOCK_FRAMES && ms == IO_TIMEOUT_MS && pcm[0] == 0);
    *sent = frames;
    return ESP_OK;
}

esp_err_t candis_audio_playback_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t ms)
{
    assert(amp == (applied_volume > 0) && frames == BLOCK_FRAMES && ms == IO_TIMEOUT_MS);
    ++writes;
    if (exercise_volume && writes == 1) {
        assert(applied_volume == 0);
        recorder_set_volume(RECORDER_MAX_VOLUME);
    } else if (exercise_volume && writes == 2) {
        assert(applied_volume == RECORDER_MAX_VOLUME);
        recorder_set_volume(0);
    } else if (exercise_volume && writes == 3) {
        assert(applied_volume == 0);
    }
    if (writes == 1) {
        first = pcm[0];
    }
    last = pcm[2 * (frames - 1)];
    if (writes == 2 && !cancel_write) {
        assert((pcm[0] == 0) == (playback_route == RECORDER_RIGHT_ONLY));
        assert((pcm[1] == 0) == (playback_route == RECORDER_LEFT_ONLY));
    }
    for (size_t frame = 0; frame < frames; ++frame) {
        if (playback_route == RECORDER_RIGHT_ONLY) {
            assert(pcm[2 * frame] == 0);
        }
        if (playback_route == RECORDER_LEFT_ONLY) {
            assert(pcm[2 * frame + 1] == 0);
        }
    }
    *sent = writes == short_write ? frames - 1 : frames;
    if (writes == cancel_write) {
        recorder_stop();
    }
    return ESP_OK;
}

esp_err_t candis_audio_set_output(bool enable)
{
    amp = enable;
    return ESP_OK;
}

esp_err_t candis_audio_set_volume(unsigned volume)
{
    applied_volume = volume;
    ++volume_changes;
    if (!volume) {
        amp = false;
    }
    return ESP_OK;
}

static void run(recorder_status_t *status, recorder_mode_t mode)
{
    const recorder_command_t command = {
        .mode = mode, .route = playback_route, .request = status->request + 1,
    };
    atomic_store(&s_stop, false);
    reads = writes = 0;
    run_command(&command, status);
}

int main(void)
{
    recorder_status_t status = {0};
    assert(recorder_init() == ESP_OK);
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_ERR_INVALID_STATE && opens == 0 && closes == 0);

    run(&status, RECORDER_RECORD);
    assert(status.result == ESP_OK && status.cleanup == ESP_OK &&
           status.progress == RECORDER_MAX_FRAMES && status.clip_frames == RECORDER_MAX_FRAMES &&
           reads == (WARMUP_FRAMES + RECORDER_MAX_FRAMES) / BLOCK_FRAMES && status.clipped_channels == 1);

    cancel_read = 2;
    run(&status, RECORDER_RECORD);
    assert(status.result == ESP_OK && status.progress == 0 && status.clip_frames == RECORDER_MAX_FRAMES);
    cancel_read = 0;

    short_read = WARMUP_FRAMES / BLOCK_FRAMES + 4;
    run(&status, RECORDER_RECORD);
    assert(status.result == ESP_ERR_INVALID_SIZE && status.clip_frames == 3 * BLOCK_FRAMES && !amp);

    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && writes == 3 && first == 0 && last == 0 && !amp);
    assert(applied_volume == RECORDER_DEFAULT_VOLUME);
    assert(s_clip[0] == INT16_MIN);

    exercise_volume = true;
    recorder_set_volume(0);
    const unsigned previous_changes = volume_changes;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && writes == 3 && volume_changes == previous_changes + 3 && !amp);
    exercise_volume = false;
    recorder_set_volume(RECORDER_MAX_VOLUME);
    recorder_set_volume(RECORDER_MAX_VOLUME + 1);

    playback_route = RECORDER_LEFT_ONLY;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && writes == 3 && !amp);
    assert(applied_volume == RECORDER_MAX_VOLUME);
    playback_route = RECORDER_RIGHT_ONLY;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && writes == 3 && !amp);
    playback_route = RECORDER_STEREO;

    cancel_write = 1;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && writes == 2 && first == 0 && last == 0 && !amp);
    cancel_write = 0;

    short_write = 1;
    fail_close = true;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_ERR_INVALID_SIZE && status.cleanup == ESP_FAIL && !amp);
    const unsigned previous_opens = opens;
    run(&status, RECORDER_PLAY);
    assert(status.cleanup == ESP_FAIL && opens == previous_opens);
    short_write = 0;
    fail_close = false;
    run(&status, RECORDER_PLAY);
    assert(status.result == ESP_OK && status.cleanup == ESP_OK && opens == previous_opens + 1 && !amp);
    assert(closes == opens + 2);
    recorder_deinit();
}
"""


class AudioRecorderTest(unittest.TestCase):
    def test_clip_cancellation_fades_and_error_recovery(self):
        root = Path(__file__).resolve().parents[1]
        idf_headers = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
        with TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / "fake.h").write_text(STUB)
            for name in ("sdkconfig.h", "esp_check.h", "esp_heap_caps.h",
                         "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h"):
                header = path / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "fake.h"\n')
            source = path / "test.c"
            source.write_text(SOURCE)
            binary = path / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                 f"-I{path}", f"-I{idf_headers}",
                 f"-I{root / 'examples/audio/main'}", f"-I{root / 'components/candis/include'}",
                 str(source), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)
