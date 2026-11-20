/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "recorder.h"
#include "sdkconfig.h"

enum {
    BLOCK_FRAMES = CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES,
    WARMUP_FRAMES = CANDIS_AUDIO_RATE * 400 / 1000,
    IO_TIMEOUT_MS = 100,
    DRAIN_MS = 55, /* The Candis BSP uses ten 5 ms TX DMA buffers. */
};

_Static_assert(RECORDER_MAX_FRAMES % BLOCK_FRAMES == 0, "The clip must contain whole blocks");
_Static_assert(WARMUP_FRAMES % BLOCK_FRAMES == 0, "Warmup must use whole blocks");

#ifdef CONFIG_CANDIS_RECORDER_MANAGE_AUDIO_POWER
static const bool s_manage_audio_power = true;
#else
static const bool s_manage_audio_power = false;
#endif

typedef struct {
    recorder_mode_t mode;
    recorder_route_t route;
    uint32_t request;
} recorder_command_t;

static const char *TAG = "candis_recorder";
static QueueHandle_t s_commands;
static QueueHandle_t s_status;
static atomic_bool s_stop;
static atomic_uint s_volume = RECORDER_DEFAULT_VOLUME;
static int16_t *s_clip;

esp_err_t recorder_init(void)
{
    s_clip = heap_caps_malloc(RECORDER_MAX_FRAMES * CANDIS_AUDIO_CHANNELS * sizeof(*s_clip),
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_commands = xQueueCreate(1, sizeof(recorder_command_t));
    s_status = xQueueCreate(1, sizeof(recorder_status_t));
    if (!s_clip || !s_commands || !s_status) {
        recorder_deinit();
        return ESP_ERR_NO_MEM;
    }
    const recorder_status_t initial = {0};
    xQueueOverwrite(s_status, &initial);
    return ESP_OK;
}

void recorder_deinit(void)
{
    if (s_commands) {
        vQueueDelete(s_commands);
        s_commands = NULL;
    }
    if (s_status) {
        vQueueDelete(s_status);
        s_status = NULL;
    }
    heap_caps_free(s_clip);
    s_clip = NULL;
}

bool recorder_start(recorder_mode_t mode, recorder_route_t route, uint32_t request)
{
    const recorder_command_t cmd = {.mode = mode, .route = route, .request = request};
    atomic_store(&s_stop, false);
    return xQueueSend(s_commands, &cmd, 0) == pdTRUE;
}

void recorder_stop(void)
{
    atomic_store(&s_stop, true);
}

void recorder_set_volume(unsigned volume)
{
    if (volume <= RECORDER_MAX_VOLUME) {
        atomic_store(&s_volume, volume);
    }
}

bool recorder_status(recorder_status_t *status)
{
    return xQueuePeek(s_status, status, 0) == pdTRUE;
}

static void measure(recorder_status_t *status, const int16_t *pcm, size_t frames, bool record)
{
    for (size_t i = 0; i < frames; ++i) {
        for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
            const int sample = pcm[i * CANDIS_AUDIO_CHANNELS + ch];
            const unsigned magnitude = sample < 0 ? -sample : sample;
            if (magnitude > status->peak[ch]) {
                status->peak[ch] = magnitude;
            }
            if (record && magnitude >= INT16_MAX) {
                status->clipped_channels |= 1U << ch;
            }
        }
    }
}

static esp_err_t read_block(int16_t *pcm)
{
    size_t frames;
    ESP_RETURN_ON_ERROR(candis_audio_read(pcm, BLOCK_FRAMES, &frames, IO_TIMEOUT_MS), TAG, "read PCM");
    return frames == BLOCK_FRAMES ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static void publish_levels(recorder_status_t *status)
{
    /* Preserve the maximum level between callbacks on the LVGL task. */
    if (status->progress % (10 * BLOCK_FRAMES) == 0) {
        xQueueOverwrite(s_status, status);
        memset(status->peak, 0, sizeof(status->peak));
    }
}

static esp_err_t record_clip(recorder_status_t *status)
{
    int16_t block[BLOCK_FRAMES * CANDIS_AUDIO_CHANNELS];
    for (size_t i = 0; i < WARMUP_FRAMES && !atomic_load(&s_stop); i += BLOCK_FRAMES) {
        ESP_RETURN_ON_ERROR(read_block(block), TAG, "settle microphone");
    }
    if (atomic_load(&s_stop)) {
        return ESP_OK;
    }
    status->active = true;
    xQueueOverwrite(s_status, status);
    while (status->progress < RECORDER_MAX_FRAMES && !atomic_load(&s_stop)) {
        ESP_RETURN_ON_ERROR(read_block(block), TAG, "record clip");
        if (atomic_load(&s_stop)) {
            break;
        }
        memcpy(s_clip + status->progress * CANDIS_AUDIO_CHANNELS, block, sizeof(block));
        status->progress += BLOCK_FRAMES;
        status->clip_frames = status->progress;
        measure(status, block, BLOCK_FRAMES, true);
        publish_levels(status);
    }
    return ESP_OK;
}

static esp_err_t play_clip(recorder_status_t *status, recorder_route_t route)
{
    int16_t block[BLOCK_FRAMES * CANDIS_AUDIO_CHANNELS] = {0};
    const bool enabled[] = {route != RECORDER_RIGHT_ONLY, route != RECORDER_LEFT_ONLY};
    size_t sent;
    unsigned applied_volume = atomic_load(&s_volume);
    ESP_RETURN_ON_ERROR(candis_audio_set_volume(applied_volume), TAG, "set playback volume");
    ESP_RETURN_ON_ERROR(candis_audio_write(block, BLOCK_FRAMES, &sent, IO_TIMEOUT_MS), TAG, "prime TX with silence");
    ESP_RETURN_ON_FALSE(sent == BLOCK_FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short TX prime");
    if (atomic_load(&s_stop)) {
        return ESP_OK;
    }
    if (applied_volume) {
        ESP_RETURN_ON_ERROR(candis_audio_set_output(true), TAG, "enable amplifier");
    }
    ESP_LOGI(TAG, "Playback volume=%u%%", applied_volume);
    status->active = !atomic_load(&s_stop);
    xQueueOverwrite(s_status, status);
    while (status->progress < status->clip_frames) {
        const bool stopping = atomic_load(&s_stop);
        if (stopping && !status->progress) {
            break;
        }
        const unsigned requested_volume = atomic_load(&s_volume);
        if (!stopping && requested_volume != applied_volume) {
            ESP_RETURN_ON_ERROR(candis_audio_set_volume(requested_volume), TAG, "adjust playback volume");
            if (!applied_volume && requested_volume) {
                ESP_RETURN_ON_ERROR(candis_audio_set_output(true), TAG, "unmute amplifier");
            }
            applied_volume = requested_volume;
            ESP_LOGI(TAG, "Playback volume=%u%%", applied_volume);
        }
        for (size_t i = 0; i < BLOCK_FRAMES; ++i) {
            const size_t position = status->progress + i;
            const size_t tail = status->clip_frames - position - 1;
            size_t ramp = position < tail ? position : tail;
            if (stopping && ramp > BLOCK_FRAMES - 1 - i) {
                ramp = BLOCK_FRAMES - 1 - i;
            }
            const int gain = ramp < BLOCK_FRAMES ? ramp : BLOCK_FRAMES;
            for (unsigned ch = 0; ch < CANDIS_AUDIO_CHANNELS; ++ch) {
                block[i * CANDIS_AUDIO_CHANNELS + ch] =
                    enabled[ch] ? (int)s_clip[position * CANDIS_AUDIO_CHANNELS + ch] * gain / BLOCK_FRAMES : 0;
            }
        }
        ESP_RETURN_ON_ERROR(candis_audio_playback_write(block, BLOCK_FRAMES, &sent, IO_TIMEOUT_MS), TAG, "play clip");
        ESP_RETURN_ON_FALSE(sent == BLOCK_FRAMES, ESP_ERR_INVALID_SIZE, TAG, "short clip write");
        status->progress += sent;
        if (applied_volume) {
            measure(status, block, sent, false);
        }
        publish_levels(status);
        if (stopping) {
            break;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(DRAIN_MS) + 1);
    return candis_audio_set_output(false);
}

static void run_command(const recorder_command_t *cmd, recorder_status_t *status)
{
    const bool retry_cleanup = status->cleanup != ESP_OK;
    *status = (recorder_status_t){
        .request = cmd->request,
        .mode = cmd->mode,
        .clip_frames = status->clip_frames,
    };
    xQueueOverwrite(s_status, status);
    if (retry_cleanup) {
        status->cleanup = candis_audio_close();
        if (status->cleanup != ESP_OK) {
            return;
        }
    }
    if (atomic_load(&s_stop)) {
        return;
    }
    if (cmd->mode == RECORDER_PLAY && !status->clip_frames) {
        status->result = ESP_ERR_INVALID_STATE;
        return;
    }
    status->result = candis_audio_open(s_manage_audio_power);
    if (status->result == ESP_OK && !atomic_load(&s_stop)) {
        status->result = cmd->mode == RECORDER_RECORD ? record_clip(status) : play_clip(status, cmd->route);
    }
    status->cleanup = candis_audio_close();
}

void recorder_run(void)
{
    recorder_status_t status = {0};
    recorder_command_t cmd;
    for (;;) {
        if (xQueueReceive(s_commands, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        run_command(&cmd, &status);
        ESP_LOGI(TAG, "%s frames=%zu clip=%zu clipped=0x%02x result=%s cleanup=%s%s",
                 cmd.mode == RECORDER_RECORD ? "record" : "playback", status.progress, status.clip_frames,
                 status.clipped_channels, esp_err_to_name(status.result), esp_err_to_name(status.cleanup),
                 atomic_load(&s_stop) ? " stopped" : "");
        status.mode = RECORDER_IDLE;
        status.active = false;
        memset(status.peak, 0, sizeof(status.peak));
        xQueueOverwrite(s_status, &status);
    }
}
