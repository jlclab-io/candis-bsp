/* SPDX-License-Identifier: Apache-2.0 */
#include "candis_example_usb.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bsp/candis_ov5640.h"
#include "control_console.h"

void camera_control_console_help(void)
{
    puts("CAM commands: READ, AUTO, MANUAL <lines> <gain_q4> <R> <G> <B>, "
         "AELEVEL <-5..5>, WB <AUTO|SUNNY|CLOUDY|OFFICE|HOME>, ISP <brightness> <contrast> <U> <V>, "
         "CIP <AUTO|MANUAL> <sharpness:-3..3> <denoise:0..8>, EFFECT <NORMAL|NEGATIVE|GRAY|RED|GREEN|BLUE|SEPIA>, "
         "SATURATION <-4..4>, GAMMA <0|1>, STOP");
}

static bool parse_numbers(const char *text, long *values, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        char *end;
        while (isspace((unsigned char)*text)) {
            ++text;
        }
        errno = 0;
        values[i] = strtol(text, &end, 10);
        if (end == text || errno == ERANGE || (*end && !isspace((unsigned char)*end))) {
            return false;
        }
        text = end;
    }
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    return *text == '\0';
}

static void show_settings(void)
{
    candis_ov5640_controls_t controls;
    uint16_t frame_length;
    esp_err_t err = candis_ov5640_get_controls(&controls, &frame_length);
    if (err == ESP_OK) {
        printf("CAM sensor auto=%d/%d/%d lines=%u/%u gain_q4=%u WB=%u/%u/%u\n",
               controls.auto_exposure, controls.auto_gain, controls.auto_white_balance,
               (unsigned)controls.exposure_lines, (unsigned)frame_length, (unsigned)controls.gain_q4,
               (unsigned)controls.red_gain_q10, (unsigned)controls.green_gain_q10, (unsigned)controls.blue_gain_q10);
    } else {
        printf("CAM sensor %s\n", esp_err_to_name(err));
    }

    candis_ov5640_isp_t isp;
    err = candis_ov5640_get_isp(&isp);
    if (err == ESP_OK) {
        printf("CAM ISP luma=%d brightness=%d contrast=%u saturation=%d manual=%d U=%u V=%u\n",
               isp.luma_enabled, isp.brightness, (unsigned)isp.contrast, isp.saturation_enabled,
               isp.manual_saturation, (unsigned)isp.saturation_u, (unsigned)isp.saturation_v);
    } else {
        printf("CAM ISP %s\n", esp_err_to_name(err));
    }
}

static esp_err_t set_manual(const char *text)
{
    long values[5];
    if (!parse_numbers(text, values, 5) || values[0] < 1 || values[0] > UINT16_MAX ||
        values[1] < 16 || values[1] > 1023 || values[2] < 1 || values[2] > 4095 ||
        values[3] < 1 || values[3] > 4095 || values[4] < 1 || values[4] > 4095) {
        return ESP_ERR_INVALID_ARG;
    }
    const candis_ov5640_controls_t controls = {
        .exposure_lines = values[0], .gain_q4 = values[1],
        .red_gain_q10 = values[2], .green_gain_q10 = values[3], .blue_gain_q10 = values[4],
    };
    return candis_ov5640_set_controls(&controls);
}

static esp_err_t set_isp(const char *text)
{
    long values[4];
    if (!parse_numbers(text, values, 4) || values[0] < -255 || values[0] > 255 ||
        values[1] < 0 || values[1] > 255 || values[2] < 0 || values[2] > 255 ||
        values[3] < 0 || values[3] > 255) {
        return ESP_ERR_INVALID_ARG;
    }
    const candis_ov5640_isp_t isp = {
        .luma_enabled = true, .saturation_enabled = true, .manual_saturation = true,
        .brightness = values[0], .contrast = values[1], .saturation_u = values[2], .saturation_v = values[3],
    };
    return candis_ov5640_set_isp(&isp);
}

static esp_err_t set_wb_preset(const char *name)
{
    static const struct {
        const char *name;
        candis_ov5640_wb_preset_t mode;
    } presets[] = {
        {"AUTO", CANDIS_OV5640_WB_AUTO}, {"SUNNY", CANDIS_OV5640_WB_SUNNY},
        {"CLOUDY", CANDIS_OV5640_WB_CLOUDY}, {"OFFICE", CANDIS_OV5640_WB_OFFICE},
        {"HOME", CANDIS_OV5640_WB_HOME},
    };
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); ++i) {
        if (!strcmp(name, presets[i].name)) {
            return candis_ov5640_set_white_balance_preset(presets[i].mode);
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t set_cip(const char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    candis_ov5640_cip_t cip = {0};
    if (!strncmp(text, "AUTO", 4) && (text[4] == '\0' || isspace((unsigned char)text[4]))) {
        text += 4;
        cip.auto_sharpness = true;
    } else if (!strncmp(text, "MANUAL", 6) && isspace((unsigned char)text[6])) {
        text += 6;
        long values[2];
        if (!parse_numbers(text, values, 2) || values[0] < -3 || values[0] > 3 ||
            values[1] < 0 || values[1] > 8) {
            return ESP_ERR_INVALID_ARG;
        }
        cip.sharpness = values[0];
        cip.denoise = values[1];
        return candis_ov5640_set_cip(&cip);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    long denoise;
    if (!parse_numbers(text, &denoise, 1) || denoise < 0 || denoise > 8) {
        return ESP_ERR_INVALID_ARG;
    }
    cip.denoise = denoise;
    return candis_ov5640_set_cip(&cip);
}

static esp_err_t set_color_effect(const char *name)
{
    static const struct {
        const char *name;
        candis_ov5640_effect_t effect;
    } effects[] = {
        {"NORMAL", CANDIS_OV5640_EFFECT_NORMAL}, {"NEGATIVE", CANDIS_OV5640_EFFECT_NEGATIVE},
        {"GRAY", CANDIS_OV5640_EFFECT_GRAYSCALE}, {"RED", CANDIS_OV5640_EFFECT_RED_TINT},
        {"GREEN", CANDIS_OV5640_EFFECT_GREEN_TINT}, {"BLUE", CANDIS_OV5640_EFFECT_BLUE_TINT},
        {"SEPIA", CANDIS_OV5640_EFFECT_SEPIA},
    };
    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); ++i) {
        if (!strcmp(name, effects[i].name)) {
            return candis_ov5640_set_color_effect(effects[i].effect);
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t set_saturation(const char *text)
{
    long value;
    if (!parse_numbers(text, &value, 1) || value < -4 || value > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    return candis_ov5640_set_cmx_saturation(value);
}

static esp_err_t set_gamma(const char *text)
{
    if (strcmp(text, "0") && strcmp(text, "1")) {
        return ESP_ERR_INVALID_ARG;
    }
    return candis_ov5640_set_gamma_preset(text[0] - '0');
}

static bool handle_line(const char *line)
{
    if (!strcmp(line, "STOP")) {
        return false;
    }
    if (!strcmp(line, "READ")) {
        show_settings();
        return true;
    }
    esp_err_t err;
    if (!strcmp(line, "AUTO")) {
        const candis_ov5640_controls_t auto_modes = {
            .auto_exposure = true, .auto_gain = true, .auto_white_balance = true,
        };
        err = candis_ov5640_set_controls(&auto_modes);
    } else if (!strncmp(line, "MANUAL ", 7)) {
        err = set_manual(line + 7);
    } else if (!strncmp(line, "AELEVEL ", 8)) {
        long value;
        if (!parse_numbers(line + 8, &value, 1) || value < -5 || value > 5) {
            err = ESP_ERR_INVALID_ARG;
        } else {
            err = candis_ov5640_set_ae_level(value);
        }
    } else if (!strncmp(line, "WB ", 3)) {
        err = set_wb_preset(line + 3);
    } else if (!strncmp(line, "CIP ", 4)) {
        err = set_cip(line + 4);
    } else if (!strncmp(line, "EFFECT ", 7)) {
        err = set_color_effect(line + 7);
    } else if (!strncmp(line, "SATURATION ", 11)) {
        err = set_saturation(line + 11);
    } else if (!strncmp(line, "GAMMA ", 6)) {
        err = set_gamma(line + 6);
    } else if (!strncmp(line, "ISP ", 4)) {
        err = set_isp(line + 4);
    } else {
        camera_control_console_help();
        return true;
    }
    printf("CAM %s\n", err == ESP_OK ? "scheduled" : esp_err_to_name(err));
    return true;
}

bool camera_control_console_poll(void)
{
    static char line[64];
    static size_t length;
    static bool discard;
    /* Limit nonblocking console input processing to one command per frame. */
    for (unsigned i = 0; i < sizeof(line); ++i) {
        char byte;
        if (!candis_example_usb_read(&byte)) {
            break;
        }
        if (byte == '\n' || byte == '\r') {
            if (discard) {
                puts("CAM invalid or overlong input");
            } else if (length) {
                line[length] = '\0';
                length = 0;
                return handle_line(line);
            }
            discard = false;
            length = 0;
        } else if ((byte >= ' ' && byte <= '~') || byte == '\t') {
            if (length < sizeof(line) - 1) {
                line[length++] = byte;
            } else {
                discard = true;
            }
        } else {
            discard = true;
        }
    }
    return true;
}
