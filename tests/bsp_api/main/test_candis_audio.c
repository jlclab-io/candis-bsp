/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <string.h>
#include "api_test.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"

#define CAPI "candis"

#if BSP_CAPS_AUDIO
static esp_codec_dev_sample_info_t codec_format(void)
{
    return (esp_codec_dev_sample_info_t){
        .sample_rate = CANDIS_AUDIO_RATE,
        .bits_per_sample = 16,
        .channel = CANDIS_AUDIO_CHANNELS,
        .channel_mask = 3,
        .mclk_multiple = 256,
    };
}

void api_tests_candis_audio(void)
{
    i2s_std_config_t invalid = {0};
    esp_codec_dev_handle_t speaker = NULL;
    esp_codec_dev_handle_t microphone = NULL;
    api_test_rail_state_t aldo2 = {0};
    const bool rail_captured = api_test_rail_capture(TG28_ESP_RAIL_ALDO2, &aldo2) == ESP_OK;
    const bool rail_ready = rail_captured &&
                            api_test_rail_force(TG28_ESP_RAIL_ALDO2, 3300, true) == ESP_OK;

    api_test_require(CAPI, "bsp_audio_init", API_ASPECT_BOUNDARY,
                     bsp_audio_init(&invalid), ESP_ERR_NOT_SUPPORTED,
                     "unsupported I2S configuration");
    api_test_check_bool(CAPI, "bsp_audio_get_codec_itf", API_ASPECT_ERROR,
                        bsp_audio_get_codec_itf() == NULL, "NULL interface before audio init");

    esp_err_t err = rail_ready ? bsp_audio_init(NULL) : ESP_ERR_INVALID_STATE;
    if (rail_captured) {
        api_test_require(CAPI, "bsp_audio_init", API_ASPECT_NORMAL, err, ESP_OK,
                         "initialize ES8389 and I2S with powered ALDO2");
    } else {
        api_test_check_bool(CAPI, "bsp_audio_init", API_ASPECT_ERROR,
                            err == ESP_ERR_INVALID_STATE, "existing ALDO2 is required");
        API_BLOCKED(CAPI, "bsp_audio_init", API_ASPECT_HARDWARE,
                    "ALDO2 could not be captured and enabled safely");
    }
    if (err == ESP_OK) {
        const audio_codec_data_if_t *itf = bsp_audio_get_codec_itf();
        api_test_check_bool(CAPI, "bsp_audio_get_codec_itf", API_ASPECT_NORMAL,
                            itf != NULL, "borrowed codec data interface");
        speaker = bsp_audio_codec_speaker_init();
        microphone = bsp_audio_codec_microphone_init();
        api_test_check_bool(CAPI, "bsp_audio_codec_speaker_init", API_ASPECT_NORMAL,
                            speaker != NULL, "speaker codec handle");
        api_test_check_bool(CAPI, "bsp_audio_codec_microphone_init", API_ASPECT_NORMAL,
                            microphone != NULL, "microphone codec handle");
        api_test_check_bool(CAPI, "bsp_audio_codec_speaker_init", API_ASPECT_NORMAL,
                            speaker == microphone, "shared duplex codec");
        if (speaker) {
            esp_codec_dev_sample_info_t format = codec_format();
            const esp_err_t open = esp_codec_dev_open(speaker, &format);
            api_test_check_bool(CAPI, "bsp_audio_codec_speaker_init", API_ASPECT_HARDWARE,
                                open == ESP_CODEC_DEV_OK, "codec duplex open");
            if (open == ESP_CODEC_DEV_OK) {
                api_test_log(CAPI, "bsp_audio_codec_speaker_init", API_ASPECT_NORMAL, API_PASS,
                             "codec opened and closed successfully");
                (void)esp_codec_dev_close(speaker);
            }
        }
        api_test_require(CAPI, "candis_audio_open", API_ASPECT_ERROR,
                         candis_audio_open(true), ESP_ERR_INVALID_STATE,
                         "Candis audio owner rejects second owner");
        api_test_require(CAPI, "bsp_audio_deinit", API_ASPECT_NORMAL,
                         bsp_audio_deinit(), ESP_OK, "release BSP audio owner");
        api_test_require(CAPI, "bsp_audio_deinit", API_ASPECT_LIFECYCLE,
                         bsp_audio_deinit(), ESP_OK, "repeated BSP audio release");
    } else {
        API_BLOCKED(CAPI, "candis_audio_open", API_ASPECT_HARDWARE,
                    "BSP audio initialization failed");
    }

    int16_t silence[2 * CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES] = {0};
    size_t transferred = 123;
    api_test_require(CAPI, "candis_audio_read", API_ASPECT_BOUNDARY,
                     candis_audio_read(NULL, 1, &transferred, 1), ESP_ERR_INVALID_ARG,
                     "NULL PCM");
    api_test_require(CAPI, "candis_audio_read", API_ASPECT_BOUNDARY,
                     candis_audio_read(silence, 0, &transferred, 1), ESP_ERR_INVALID_ARG,
                     "zero frames");
    api_test_require(CAPI, "candis_audio_read", API_ASPECT_BOUNDARY,
                     candis_audio_read(silence, 1, NULL, 1), ESP_ERR_INVALID_ARG,
                     "NULL received count");
    api_test_require(CAPI, "candis_audio_write", API_ASPECT_BOUNDARY,
                     candis_audio_write(NULL, 1, &transferred, 1), ESP_ERR_INVALID_ARG,
                     "NULL PCM");
    api_test_require(CAPI, "candis_audio_write", API_ASPECT_BOUNDARY,
                     candis_audio_write(silence, 0, &transferred, 1), ESP_ERR_INVALID_ARG,
                     "zero frames");
    api_test_require(CAPI, "candis_audio_playback_write", API_ASPECT_BOUNDARY,
                     candis_audio_playback_write(silence, CANDIS_AUDIO_PLAYBACK_BLOCK_FRAMES + 1,
                                                 &transferred, 1), ESP_ERR_INVALID_ARG,
                     "block exceeds 240 frames");
    api_test_require(CAPI, "candis_audio_set_volume", API_ASPECT_BOUNDARY,
                     candis_audio_set_volume(101), ESP_ERR_INVALID_ARG, "volume above 100");
    api_test_require(CAPI, "candis_audio_close", API_ASPECT_LIFECYCLE,
                     candis_audio_close(), ESP_OK, "close is safe before open");

    err = candis_audio_open(true);
    api_test_require(CAPI, "candis_audio_open", API_ASPECT_NORMAL, err, ESP_OK,
                     "open duplex PCM with managed ALDO2");
    if (err == ESP_OK) {
        api_test_require(CAPI, "candis_audio_set_volume", API_ASPECT_NORMAL,
                         candis_audio_set_volume(50), ESP_OK, "set 50 percent volume");
        api_test_require(CAPI, "candis_audio_set_output", API_ASPECT_NORMAL,
                         candis_audio_set_output(true), ESP_OK, "enable amplifier at nonzero volume");
        api_test_require(CAPI, "candis_audio_set_output", API_ASPECT_LIFECYCLE,
                         candis_audio_set_output(false), ESP_OK, "disable amplifier");
        api_test_require(CAPI, "candis_audio_set_mute", API_ASPECT_NORMAL,
                         candis_audio_set_mute(true), ESP_OK, "mute codec");
        api_test_require(CAPI, "candis_audio_set_mute", API_ASPECT_NORMAL,
                         candis_audio_set_mute(false), ESP_OK, "unmute codec");
        api_test_require(CAPI, "candis_audio_set_volume", API_ASPECT_LIFECYCLE,
                         candis_audio_set_volume(0), ESP_OK, "zero volume disables amplifier");
        api_test_require(CAPI, "candis_audio_set_output", API_ASPECT_ERROR,
                         candis_audio_set_output(true), ESP_ERR_INVALID_STATE,
                         "amplifier cannot enable at zero volume");
        api_test_require(CAPI, "candis_audio_set_volume", API_ASPECT_NORMAL,
                         candis_audio_set_volume(100), ESP_OK, "set maximum volume");
        api_test_require(CAPI, "candis_audio_read", API_ASPECT_NORMAL,
                         candis_audio_read(silence, 1, &transferred, 1), ESP_OK,
                         "read one duplex frame");
        api_test_check_bool(CAPI, "candis_audio_read", API_ASPECT_HARDWARE,
                            transferred == 1, "one frame transferred");
        api_test_require(CAPI, "candis_audio_write", API_ASPECT_NORMAL,
                         candis_audio_write(silence, 1, &transferred, 1), ESP_OK,
                         "write one silent duplex frame");
        api_test_check_bool(CAPI, "candis_audio_write", API_ASPECT_HARDWARE,
                            transferred == 1, "one frame queued");
        api_test_require(CAPI, "candis_audio_playback_write", API_ASPECT_NORMAL,
                         candis_audio_playback_write(silence, 1, &transferred, 1), ESP_OK,
                         "playback writer accepts one frame");
        api_test_require(CAPI, "candis_audio_close", API_ASPECT_NORMAL,
                         candis_audio_close(), ESP_OK, "close audio owner");
        api_test_require(CAPI, "candis_audio_close", API_ASPECT_LIFECYCLE,
                         candis_audio_close(), ESP_OK, "repeated close");
        API_BLOCKED(CAPI, "candis_audio_open", API_ASPECT_HARDWARE,
                    "acoustic output and microphone signal need external observation");
    }
    if (rail_ready) {
        api_test_check_bool("api-test", "restore_aldo2", API_ASPECT_LIFECYCLE,
                            api_test_rail_restore(TG28_ESP_RAIL_ALDO2, &aldo2) == ESP_OK,
                            "restore ALDO2 after BSP and Candis audio tests");
    }
}
#else
void api_tests_candis_audio(void)
{
    const char *apis[] = {
        "bsp_audio_init", "bsp_audio_get_codec_itf", "bsp_audio_codec_speaker_init",
        "bsp_audio_codec_microphone_init", "bsp_audio_deinit", "candis_audio_open",
        "candis_audio_set_volume", "candis_audio_set_mute", "candis_audio_set_output",
        "candis_audio_read", "candis_audio_write", "candis_audio_playback_write",
        "candis_audio_close",
    };
    for (size_t i = 0; i < sizeof(apis) / sizeof(apis[0]); ++i) {
        API_NA(CAPI, apis[i], API_ASPECT_HARDWARE, "CONFIG_CANDIS_AUDIO disabled");
    }
}
#endif
