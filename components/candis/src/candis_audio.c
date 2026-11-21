/* SPDX-License-Identifier: Apache-2.0 */
/* Candis hardware revision 1.2, ES8389 with dual NS4150B amplifiers. */
#include <math.h>
#include <stdatomic.h>
#include "bsp/audio.h"
#include "bsp/candis_audio.h"
#include "candis_internal.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FRAME_BYTES (sizeof(int16_t) * CANDIS_AUDIO_CHANNELS)
#define MCLK_MULTIPLE 256
#define MIC_GAIN_REQUEST_DB 30
#define ES8389_PGA_LEFT 0x72
#define ES8389_PGA_RIGHT 0x73
#define ES8389_PGA_30_5DB 0x0c
#define ES8389_DAC_LEFT 0x46
#define ES8389_DAC_RIGHT 0x47
#define ES8389_DAC_MINUS_6DB 0xb3
#define DAC_MAX_GAIN_DB (-6.0f)
#define MAX_VOLUME 100
#define PLAYBACK_GAIN 4.0f
#define PLAYBACK_PEAK 25000.0f
#define PLAYBACK_RELEASE_FRAMES (CANDIS_AUDIO_RATE / 10)

static const char *TAG = "candis_audio";
static portMUX_TYPE s_audio_lock = portMUX_INITIALIZER_UNLOCKED;
enum {
    AUDIO_RX = 1U << 0,
    AUDIO_TX = 1U << 1,
    AUDIO_CONTROL = 1U << 2,
};
typedef enum {
    AUDIO_UNOWNED,
    AUDIO_CANDIS,
    AUDIO_BSP,
} audio_owner_t;

static struct {
    candis_power_t power;
    i2s_chan_handle_t tx;
    i2s_chan_handle_t rx;
    const audio_codec_ctrl_if_t *i2c;
    const audio_codec_if_t *codec;
    const audio_codec_data_if_t *i2s;
    esp_codec_dev_handle_t pcm;
    audio_owner_t owner;
    bool pa_configured;
    bool open;
    bool muted;
    unsigned in_flight;
    unsigned volume;
    float playback_gain;
    atomic_bool output_enabled;
    atomic_bool speaker_requested;
} s_audio;

/* Keep this lock out of the driver calls. RX and TX remain independent. */
static bool claim_audio(unsigned operation)
{
    if (xPortInIsrContext()) {
        return false;
    }
    portENTER_CRITICAL(&s_audio_lock);
    const bool allowed = s_audio.open && !(s_audio.in_flight & operation);
    if (allowed) {
        s_audio.in_flight |= operation;
    }
    portEXIT_CRITICAL(&s_audio_lock);
    return allowed;
}

static void finish_audio(unsigned operation)
{
    portENTER_CRITICAL(&s_audio_lock);
    s_audio.in_flight &= ~operation;
    portEXIT_CRITICAL(&s_audio_lock);
}

static esp_err_t set_codec_volume(unsigned volume)
{
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(s_audio.pcm, volume) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "set ES8389 DAC volume");
    if (volume == MAX_VOLUME) {
        int left = 0, right = 0;
        ESP_RETURN_ON_FALSE(esp_codec_dev_read_reg(s_audio.pcm, ES8389_DAC_LEFT, &left) == ESP_CODEC_DEV_OK &&
                            esp_codec_dev_read_reg(s_audio.pcm, ES8389_DAC_RIGHT, &right) == ESP_CODEC_DEV_OK &&
                            left == ES8389_DAC_MINUS_6DB && right == ES8389_DAC_MINUS_6DB,
                            ESP_ERR_INVALID_RESPONSE, TAG, "ES8389 maximum DAC readback L=0x%02x R=0x%02x", left, right);
        ESP_LOGD(TAG, "DAC verified at -6 dB on both channels");
    }
    return ESP_OK;
}

static esp_err_t open_channels(const i2s_std_config_t *requested)
{
    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    channels.auto_clear_after_cb = true;
    channels.dma_desc_num = 10;
    channels.dma_frame_num = 240;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channels, &s_audio.tx, &s_audio.rx), TAG, "allocate I2S");
    const i2s_std_config_t defaults = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CANDIS_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CANDIS_I2S_MCLK, .bclk = CANDIS_I2S_BCLK, .ws = CANDIS_I2S_WS,
            .dout = CANDIS_I2S_DOUT, .din = CANDIS_I2S_DIN,
        },
    };
    const i2s_std_config_t *config = requested ? requested : &defaults;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.tx, config), TAG, "configure I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_audio.rx, config), TAG, "configure I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_audio.tx), TAG, "start I2S TX");
    return i2s_channel_enable(s_audio.rx);
}

static bool bsp_data_is_open(const audio_codec_data_if_t *data)
{
    (void)data;
    return s_audio.i2s->is_open(s_audio.i2s);
}

static int bsp_data_enable(const audio_codec_data_if_t *data, esp_codec_dev_type_t direction, bool enable)
{
    (void)data;
    if (!enable && (direction & ESP_CODEC_DEV_TYPE_OUT)) {
        atomic_store(&s_audio.output_enabled, false);
        if (s_audio.pa_configured && gpio_set_level(CANDIS_PA_ENABLE, 0) != ESP_OK) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
    }
    return s_audio.i2s->enable(s_audio.i2s, direction, enable);
}

static int bsp_data_set_format(const audio_codec_data_if_t *data, esp_codec_dev_type_t direction,
                                esp_codec_dev_sample_info_t *format)
{
    (void)data;
    return s_audio.i2s->set_fmt(s_audio.i2s, direction, format);
}

static int bsp_data_read(const audio_codec_data_if_t *data, uint8_t *pcm, int bytes)
{
    (void)data;
    if (!claim_audio(AUDIO_RX)) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    const int err = s_audio.i2s->read(s_audio.i2s, pcm, bytes);
    finish_audio(AUDIO_RX);
    return err;
}

static int bsp_data_write(const audio_codec_data_if_t *data, uint8_t *pcm, int bytes)
{
    (void)data;
    if (!claim_audio(AUDIO_TX)) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    int err = ESP_CODEC_DEV_OK;
    if (!atomic_load(&s_audio.speaker_requested)) {
        err = ESP_CODEC_DEV_NOT_SUPPORT;
    } else if (!atomic_load(&s_audio.output_enabled)) {
        uint8_t silence[CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES * FRAME_BYTES] = {0};
        err = s_audio.i2s->write(s_audio.i2s, silence, sizeof(silence));
        if (err == ESP_CODEC_DEV_OK && candis_audio_set_output(true) != ESP_OK) {
            err = ESP_CODEC_DEV_DRV_ERR;
        }
    }
    if (err == ESP_CODEC_DEV_OK) {
        err = s_audio.i2s->write(s_audio.i2s, pcm, bytes);
    }
    finish_audio(AUDIO_TX);
    return err;
}

static const audio_codec_data_if_t s_bsp_data = {
    .is_open = bsp_data_is_open,
    .enable = bsp_data_enable,
    .set_fmt = bsp_data_set_format,
    .read = bsp_data_read,
    .write = bsp_data_write,
};

static esp_err_t open_codec(void)
{
    ESP_RETURN_ON_ERROR(i2c_master_probe(s_audio.power.bus, CANDIS_CODEC_ADDR, 100), TAG, "probe ES8389");
    audio_codec_i2c_cfg_t bus = {
        /* The codec component uses the address byte, including the R/W bit position. */
        .bus_handle = s_audio.power.bus, .addr = CANDIS_CODEC_ADDR << 1, .clock_speed_hz = 100000,
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
    const bool bsp = s_audio.owner == AUDIO_BSP;
    esp_codec_dev_cfg_t pcm = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_audio.codec,
        .data_if = bsp ? &s_bsp_data : s_audio.i2s,
    };
    s_audio.pcm = esp_codec_dev_new(&pcm);
    ESP_RETURN_ON_FALSE(s_audio.pcm, ESP_ERR_NO_MEM, TAG, "create codec device");
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16, .channel = CANDIS_AUDIO_CHANNELS, .channel_mask = 3,
        .sample_rate = CANDIS_AUDIO_RATE, .mclk_multiple = MCLK_MULTIPLE,
    };
    if (bsp) {
        ESP_RETURN_ON_FALSE(esp_codec_set_disable_when_closed(s_audio.pcm, false) == ESP_CODEC_DEV_OK,
                            ESP_FAIL, TAG, "release the amplifier before the codec");
    } else {
        ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_audio.pcm, &format) == ESP_CODEC_DEV_OK,
                            ESP_FAIL, TAG, "start ES8389");
    }
    /* ES8389 DAC volume curve optimized for dual 8-ohm speaker loads. */
    esp_codec_dev_vol_map_t levels[MAX_VOLUME + 1] = {{.vol = 0, .db_value = -96.0f}};
    for (unsigned i = 1; i <= MAX_VOLUME; ++i) {
        levels[i] = (esp_codec_dev_vol_map_t){
            .vol = i, .db_value = DAC_MAX_GAIN_DB + 20.0f * log10f((float)i / MAX_VOLUME),
        };
    }
    esp_codec_dev_vol_curve_t curve = {.vol_map = levels, .count = MAX_VOLUME + 1};
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_vol_curve(s_audio.pcm, &curve) == ESP_CODEC_DEV_OK &&
                        esp_codec_dev_set_in_gain(s_audio.pcm, MIC_GAIN_REQUEST_DB) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "configure ES8389 gains");
    if (bsp) {
        ESP_RETURN_ON_FALSE(esp_codec_dev_set_out_vol(s_audio.pcm, MAX_VOLUME) == ESP_CODEC_DEV_OK,
                            ESP_FAIL, TAG, "configure default playback volume");
        s_audio.volume = MAX_VOLUME;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(set_codec_volume(MAX_VOLUME), TAG, "configure default playback volume");
    int left = 0, right = 0;
    ESP_RETURN_ON_FALSE(esp_codec_dev_read_reg(s_audio.pcm, ES8389_PGA_LEFT, &left) == ESP_CODEC_DEV_OK &&
                        esp_codec_dev_read_reg(s_audio.pcm, ES8389_PGA_RIGHT, &right) == ESP_CODEC_DEV_OK &&
                        (left & 0x0f) == ES8389_PGA_30_5DB && (right & 0x0f) == ES8389_PGA_30_5DB,
                        ESP_ERR_INVALID_RESPONSE, TAG, "verify ES8389 30.5 dB PGA");
    s_audio.volume = MAX_VOLUME;
    s_audio.playback_gain = PLAYBACK_GAIN;
    ESP_LOGD(TAG, "PGA verified at 30.5 dB on both channels");
    return ESP_OK;
}

static esp_err_t release_channel(i2s_chan_handle_t *channel)
{
    if (!*channel) {
        return ESP_OK;
    }
    i2s_chan_info_t info;
    ESP_RETURN_ON_ERROR(i2s_channel_get_info(*channel, &info), TAG, "read I2S state");
    if (info.is_enabled) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(*channel), TAG, "stop I2S channel");
    }
    const esp_err_t err = i2s_del_channel(*channel);
    if (err == ESP_OK) {
        *channel = NULL;
    }
    return err;
}

static esp_err_t release_audio(void)
{
    portENTER_CRITICAL(&s_audio_lock);
    if (s_audio.in_flight) {
        portEXIT_CRITICAL(&s_audio_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.open = false;
    portEXIT_CRITICAL(&s_audio_lock);
    atomic_store(&s_audio.output_enabled, false);
    atomic_store(&s_audio.speaker_requested, false);
    if (s_audio.pa_configured) {
        ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_PA_ENABLE, 0), TAG, "turn amplifier off");
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
        ESP_RETURN_ON_ERROR(gpio_reset_pin(CANDIS_PA_ENABLE), TAG, "release amplifier pin");
        s_audio.pa_configured = false;
    }
    s_audio.volume = 0;
    s_audio.muted = false;
    s_audio.owner = AUDIO_UNOWNED;
    return ESP_OK;
}

static esp_err_t start_audio(bool manage_power, const i2s_std_config_t *config)
{
    const gpio_config_t pa = {.pin_bit_mask = 1ULL << CANDIS_PA_ENABLE, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_PA_ENABLE, 0), TAG, "preset amplifier GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "configure amplifier GPIO");
    s_audio.pa_configured = true;
    s_audio.power.rail = CANDIS_RAIL_AUDIO;
    ESP_RETURN_ON_ERROR(candis_power_acquire(&s_audio.power, manage_power, 100), TAG, "prepare ALDO2");
    ESP_RETURN_ON_ERROR(open_channels(config), TAG, "prepare I2S");
    return open_codec();
}

static esp_err_t open_audio(audio_owner_t owner, bool manage_power, const i2s_std_config_t *config)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (owner == AUDIO_BSP && s_audio.owner == owner && s_audio.open) {
        candis_control_leave();
        return ESP_OK;
    }
    if (s_audio.owner != AUDIO_UNOWNED) {
        candis_control_leave();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.owner = owner;
    esp_err_t err = start_audio(manage_power, config);
    portENTER_CRITICAL(&s_audio_lock);
    s_audio.open = err == ESP_OK;
    portEXIT_CRITICAL(&s_audio_lock);
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(release_audio());
    }
    candis_control_leave();
    return err;
}

esp_err_t candis_audio_open(bool manage_power)
{
    return open_audio(AUDIO_CANDIS, manage_power, NULL);
}

esp_err_t bsp_audio_init(const i2s_std_config_t *config)
{
    if (config && (config->clk_cfg.sample_rate_hz != CANDIS_AUDIO_RATE ||
                   config->clk_cfg.mclk_multiple != I2S_MCLK_MULTIPLE_256 ||
                   config->slot_cfg.data_bit_width != I2S_DATA_BIT_WIDTH_16BIT ||
                   config->slot_cfg.slot_mode != I2S_SLOT_MODE_STEREO ||
                   config->slot_cfg.slot_mask != I2S_STD_SLOT_BOTH ||
                   config->gpio_cfg.mclk != CANDIS_I2S_MCLK ||
                   config->gpio_cfg.bclk != CANDIS_I2S_BCLK ||
                   config->gpio_cfg.ws != CANDIS_I2S_WS ||
                   config->gpio_cfg.dout != CANDIS_I2S_DOUT ||
                   config->gpio_cfg.din != CANDIS_I2S_DIN ||
                   config->gpio_cfg.invert_flags.mclk_inv || config->gpio_cfg.invert_flags.bclk_inv ||
                   config->gpio_cfg.invert_flags.ws_inv)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return open_audio(AUDIO_BSP, false, config);
}

const audio_codec_data_if_t *bsp_audio_get_codec_itf(void)
{
    if (!candis_control_enter()) {
        return NULL;
    }
    const audio_codec_data_if_t *data = s_audio.owner == AUDIO_BSP && s_audio.open ? s_audio.i2s : NULL;
    candis_control_leave();
    return data;
}

static esp_codec_dev_handle_t get_bsp_codec(bool speaker)
{
    if (bsp_audio_init(NULL) != ESP_OK || !candis_control_enter()) {
        return NULL;
    }
    esp_codec_dev_handle_t pcm = NULL;
    if (s_audio.owner == AUDIO_BSP && s_audio.open) {
        if (speaker) {
            atomic_store(&s_audio.speaker_requested, true);
        }
        pcm = s_audio.pcm;
    }
    candis_control_leave();
    return pcm;
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    return get_bsp_codec(true);
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    return get_bsp_codec(false);
}

esp_err_t bsp_audio_deinit(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_audio.owner == AUDIO_CANDIS ? ESP_ERR_INVALID_STATE : release_audio();
    candis_control_leave();
    return err;
}

static esp_err_t set_output(bool on)
{
    if (on && (!s_audio.volume || s_audio.muted)) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_audio.output_enabled, false);
    ESP_RETURN_ON_ERROR(gpio_set_level(CANDIS_PA_ENABLE, on), TAG, "set power amplifier state");
    if (on) {
        vTaskDelay(pdMS_TO_TICKS(120) + 1);
        atomic_store(&s_audio.output_enabled, true);
    }
    return ESP_OK;
}

esp_err_t candis_audio_set_volume(unsigned volume)
{
    if (volume > MAX_VOLUME) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!claim_audio(AUDIO_CONTROL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    if (!volume) {
        result = set_output(false);
    }
    if (result == ESP_OK) {
        result = set_codec_volume(volume);
    }
    if (result == ESP_ERR_INVALID_RESPONSE) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(set_output(false));
        s_audio.volume = 0;
    }
    if (result == ESP_OK) {
        s_audio.volume = volume;
    }
    finish_audio(AUDIO_CONTROL);
    return result;
}

esp_err_t candis_audio_set_output(bool on)
{
    if (!claim_audio(AUDIO_CONTROL)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = set_output(on);
    finish_audio(AUDIO_CONTROL);
    return err;
}

esp_err_t candis_audio_set_mute(bool mute)
{
    if (!claim_audio(AUDIO_CONTROL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = mute ? set_output(false) : ESP_OK;
    if (err == ESP_OK) {
        err = esp_codec_dev_set_out_mute(s_audio.pcm, mute) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
    }
    if (err == ESP_OK) {
        s_audio.muted = mute;
    }
    finish_audio(AUDIO_CONTROL);
    return err;
}

esp_err_t candis_audio_read(int16_t *pcm, size_t frames, size_t *received, uint32_t timeout_ms)
{
    if (!pcm || !received || !frames || frames > SIZE_MAX / FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    *received = 0;
    if (!claim_audio(AUDIO_RX)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_audio.rx, pcm, frames * FRAME_BYTES, &bytes, timeout_ms);
    finish_audio(AUDIO_RX);
    *received = bytes / FRAME_BYTES;
    return err;
}

static esp_err_t write_frames(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms)
{
    size_t bytes = 0;
    const esp_err_t err = i2s_channel_write(s_audio.tx, pcm, frames * FRAME_BYTES, &bytes, timeout_ms);
    *sent = bytes / FRAME_BYTES;
    return err;
}

esp_err_t candis_audio_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms)
{
    if (!pcm || !sent || !frames || frames > SIZE_MAX / FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    *sent = 0;
    if (!claim_audio(AUDIO_TX)) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t err = write_frames(pcm, frames, sent, timeout_ms);
    finish_audio(AUDIO_TX);
    return err;
}

esp_err_t candis_audio_playback_write(const int16_t *pcm, size_t frames, size_t *sent, uint32_t timeout_ms)
{
    if (!pcm || !sent || !frames || frames > CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    *sent = 0;
    if (!claim_audio(AUDIO_TX)) {
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
    const esp_err_t err = write_frames(block, frames, sent, timeout_ms);
    finish_audio(AUDIO_TX);
    return err;
}

esp_err_t candis_audio_close(void)
{
    if (!candis_control_enter()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = s_audio.owner == AUDIO_BSP ? ESP_ERR_INVALID_STATE : release_audio();
    candis_control_leave();
    return err;
}
