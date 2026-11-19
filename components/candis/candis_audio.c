/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2, ES8389 with dual NS4150B amplifiers. */
#include <math.h>
#include <stdatomic.h>
#include "bsp/candis_audio.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PA_ENABLE GPIO_NUM_4
#define FRAME_BYTES (sizeof(int16_t) * CANDIS_AUDIO_CHANNELS)
#define CODEC_ADDRESS 0x10
#define MCLK_MULTIPLE 256
#define MIC_GAIN_REQUEST_DB 30
#define ES8389_PGA_LEFT 0x72
#define ES8389_PGA_RIGHT 0x73
#define ES8389_PGA_30_5DB 0x0c
#define ES8389_DAC_LEFT 0x46
#define ES8389_DAC_RIGHT 0x47
#define ES8389_DAC_MINUS_6DB 0xb3
#define PLAYBACK_GAIN 4.0f
#define PLAYBACK_PEAK 25000.0f
#define PLAYBACK_RELEASE_FRAMES (CANDIS_AUDIO_RATE / 10)

static const char *TAG = "candis_audio";
static struct {
    candis_power_t power;
    i2s_chan_handle_t tx;
    i2s_chan_handle_t rx;
    const audio_codec_ctrl_if_t *i2c;
    const audio_codec_if_t *codec;
    const audio_codec_data_if_t *i2s;
    esp_codec_dev_handle_t pcm;
    bool pa_configured;
    bool open;
    unsigned volume;
    float playback_gain;
    atomic_bool output_enabled;
} s_audio;

static esp_err_t open_channels(void)
{
    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channels.auto_clear_after_cb = true;
    channels.dma_desc_num = 10;
    channels.dma_frame_num = 240;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channels, &s_audio.tx, &s_audio.rx), TAG, "allocate I2S");
    const i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CANDIS_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_15, .bclk = GPIO_NUM_16, .ws = GPIO_NUM_17,
            .dout = GPIO_NUM_18, .din = GPIO_NUM_19,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.tx, &config), TAG, "configure I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx, &config), TAG, "configure I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.tx), TAG, "start I2S TX");
    return i2s_channel_enable(s_audio.rx);
}

static esp_err_t open_codec(void)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_audio.power.bus, CODEC_ADDRESS, 100), TAG, "probe ES8389");
    audio_codec_i2c_cfg_t bus = {
        /* The codec component uses the address byte, including the R/W bit position. */
        .bus_handle = s_audio.power.bus, .addr = CODEC_ADDRESS << 1, .clock_speed_hz = 100000,
    };
    s_audio.i2c = audio_codec_new_i2c_ctrl(&bus);
    ESP_RETURN_ON_FALSE(s_audio.i2c, ESP_ERR_NO_MEM, TAG, "create ES8389 control");
    es8389_codec_cfg_t config = {
        .ctrl_if = s_audio.i2c, .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = GPIO_NUM_NC, .use_mclk = true, .no_dac_ref = true,
        .hw_gain = {.pa_voltage = 3.3, .codec_dac_voltage = 3.3}, .mclk_div = MCLK_MULTIPLE,
    };
    s_audio.codec = es8389_codec_new(&config);
    ESP_RETURN_ON_FALSE(s_audio.codec, ESP_FAIL, TAG, "create ES8389 codec");
    audio_codec_i2s_cfg_t i2s = {.rx_handle = s_audio.rx, .tx_handle = s_audio.tx};
    s_audio.i2s = audio_codec_new_i2s_data(&i2s);
    ESP_RETURN_ON_FALSE(s_audio.i2s, ESP_ERR_NO_MEM, TAG, "create codec I2S interface");
    esp_codec_dev_cfg_t pcm = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = s_audio.codec, .data_if = s_audio.i2s};
    s_audio.pcm = esp_codec_dev_new(&pcm);
    ESP_RETURN_ON_FALSE(s_audio.pcm, ESP_ERR_NO_MEM, TAG, "create codec device");
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16, .channel = CANDIS_AUDIO_CHANNELS, .channel_mask = 3,
        .sample_rate = CANDIS_AUDIO_RATE, .mclk_multiple = MCLK_MULTIPLE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_audio.pcm, &format) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "start ES8389");
    esp_codec_dev_vol_map_t levels[101] = {{.vol = 0, .db_value = -96.0f}};
    for (unsigned i = 1; i <= 100; ++i) {
        levels[i] = (esp_codec_dev_vol_map_t){.vol = i, .db_value = -6.0f + 20.0f * log10f(i / 100.0f)};
    }
    esp_codec_dev_vol_curve_t curve = {.vol_map = levels, .count = 101};
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_vol_curve(s_audio.pcm, &curve) == ESP_CODEC_DEV_OK &&
                        esp_codec_dev_set_out_vol(s_audio.pcm, 50) == ESP_CODEC_DEV_OK &&
                        esp_codec_dev_set_in_gain(s_audio.pcm, MIC_GAIN_REQUEST_DB) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "configure ES8389 gains");
    int left = 0, right = 0;
    ESP_RETURN_ON_FALSE(esp_codec_dev_read_reg(s_audio.pcm, ES8389_PGA_LEFT, &left) == ESP_CODEC_DEV_OK &&
                        esp_codec_dev_read_reg(s_audio.pcm, ES8389_PGA_RIGHT, &right) == ESP_CODEC_DEV_OK &&
                        (left & 0x0f) == ES8389_PGA_30_5DB && (right & 0x0f) == ES8389_PGA_30_5DB,
                        ESP_ERR_INVALID_RESPONSE, TAG, "verify ES8389 30.5 dB PGA");
    s_audio.volume = 50;
    s_audio.playback_gain = PLAYBACK_GAIN;
    ESP_LOGD(TAG, "PGA verified at 30.5 dB on both channels");
    return ESP_OK;
}

static esp_err_t release_channel(i2s_chan_handle_t *channel)
{
    if (!*channel) {
        return ESP_OK;
    }
    esp_err_t err = i2s_channel_disable(*channel);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = i2s_del_channel(*channel);
    if (err == ESP_OK) {
        *channel = NULL;
    }
    return err;
}

static esp_err_t release_audio(void)
{
    s_audio.open = false;
    atomic_store(&s_audio.output_enabled, false);
    if (s_audio.pa_configured) {
        ESP_RETURN_ON_ERROR(gpio_set_level(PA_ENABLE, 0), TAG, "turn amplifier off");
    }
    if (s_audio.pcm) {
        esp_codec_dev_delete(s_audio.pcm);
        s_audio.pcm = NULL;
    }
    if (s_audio.codec) {
        audio_codec_delete_codec_if(s_audio.codec);
        s_audio.codec = NULL;
    }
    if (s_audio.i2s) {
        audio_codec_delete_data_if(s_audio.i2s);
        s_audio.i2s = NULL;
    }
    if (s_audio.i2c) {
        audio_codec_delete_ctrl_if(s_audio.i2c);
        s_audio.i2c = NULL;
    }
    ESP_RETURN_ON_ERROR(release_channel(&s_audio.rx), TAG, "release I2S RX");
    ESP_RETURN_ON_ERROR(release_channel(&s_audio.tx), TAG, "release I2S TX");
    ESP_RETURN_ON_ERROR(candis_power_release(&s_audio.power), TAG, "release ALDO2");
    if (s_audio.pa_configured) {
        ESP_RETURN_ON_ERROR(gpio_reset_pin(PA_ENABLE), TAG, "release amplifier pin");
        s_audio.pa_configured = false;
    }
    s_audio.volume = 0;
    return ESP_OK;
}

static esp_err_t start_audio(bool manage_power)
{
    const gpio_config_t pa = {.pin_bit_mask = 1ULL << PA_ENABLE, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(PA_ENABLE, 0), TAG, "preset amplifier GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "configure amplifier GPIO");
    s_audio.pa_configured = true;
    s_audio.power.rail = CANDIS_RAIL_AUDIO;
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_audio.power, manage_power, 100), TAG, "prepare ALDO2");
    ESP_RETURN_ON_ERROR(open_channels(), TAG, "prepare I2S");
    return open_codec();
}

esp_err_t candis_audio_open(bool manage_power)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_audio.open || s_audio.pa_configured || s_audio.power.bus) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = start_audio(manage_power);
    s_audio.open = err == ESP_OK;
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_audio());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_audio_set_volume(unsigned volume)
{
    if (volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_audio.open) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!volume) {
        atomic_store(&s_audio.output_enabled, false);
        ESP_RETURN_ON_ERROR(gpio_set_level(PA_ENABLE, 0), TAG, "mute power amplifier");
    }
    if (esp_codec_dev_set_out_vol(s_audio.pcm, volume) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    if (volume == 100) {
        int left = 0, right = 0;
        if (esp_codec_dev_read_reg(s_audio.pcm, ES8389_DAC_LEFT, &left) != ESP_CODEC_DEV_OK ||
            esp_codec_dev_read_reg(s_audio.pcm, ES8389_DAC_RIGHT, &right) != ESP_CODEC_DEV_OK ||
            left != ES8389_DAC_MINUS_6DB || right != ES8389_DAC_MINUS_6DB) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(candis_audio_set_output(false));
            s_audio.volume = 0;
            ESP_LOGE(TAG, "ES8389 maximum DAC readback L=0x%02x R=0x%02x", left, right);
            return ESP_ERR_INVALID_RESPONSE;
        }
        ESP_LOGD(TAG, "DAC verified at -6 dB on both channels");
    }
    s_audio.volume = volume;
    return ESP_OK;
}

esp_err_t candis_audio_set_output(bool on)
{
    if (!s_audio.open || (on && !s_audio.volume)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_audio.output_enabled, false);
    ESP_RETURN_ON_ERROR(gpio_set_level(PA_ENABLE, on), TAG, "set power amplifier state");
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(120) + 1);
        atomic_store(&s_audio.output_enabled, true);
    }
    return ESP_OK;
}

esp_err_t candis_audio_read(int16_t *pcm, size_t frames, size_t *received, uint32_t timeout_ms)
{
    if (!pcm || !received || !frames || frames > SIZE_MAX / FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    *received = 0;
    if (!s_audio.open) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_audio.rx, pcm, frames * FRAME_BYTES, &bytes, timeout_ms);
    *received = bytes / FRAME_BYTES;
    return err;
}

esp_err_t candis_audio_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms)
{
    if (!pcm || !sent || !frames || frames > SIZE_MAX / FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    *sent = 0;
    if (!s_audio.open) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes = 0;
    esp_err_t err = i2s_channel_write(s_audio.tx, pcm, frames * FRAME_BYTES, &bytes, timeout_ms);
    *sent = bytes / FRAME_BYTES;
    return err;
}

esp_err_t candis_audio_playback_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms)
{
    if (!pcm || !sent || !frames || frames > CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    *sent = 0;
    if (!s_audio.open) {
        return ESP_ERR_INVALID_STATE;
    }
    int16_t block[CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES * CANDIS_AUDIO_CHANNELS] = {0};
    if (atomic_load(&s_audio.output_enabled)) {
        unsigned peak = 0;
        for (size_t i = 0; i < frames * CANDIS_AUDIO_CHANNELS; ++i) {
            unsigned magnitude = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        const float limit = peak ? fminf(PLAYBACK_GAIN, PLAYBACK_PEAK / peak) : PLAYBACK_GAIN;
        float gain = fminf(s_audio.playback_gain, limit);
        const float release = (limit - gain) / PLAYBACK_RELEASE_FRAMES;
        for (size_t i = 0; i < frames; ++i) {
            block[2 * i] = (int16_t)(pcm[2 * i] * gain);
            block[2 * i + 1] = (int16_t)(pcm[2 * i + 1] * gain);
            gain += release;
        }
        s_audio.playback_gain = fminf(gain, limit);
    } else {
        s_audio.playback_gain = PLAYBACK_GAIN;
    }
    return candis_audio_write(block, frames, sent, timeout_ms);
}

esp_err_t candis_audio_close(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = release_audio();
    candis_control_leave();
    return err;
}
