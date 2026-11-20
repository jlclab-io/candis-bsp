/* SPDX-License-Identifier: Apache-2.0 */
#include "bsp/candis_camera.h"
#include "bsp/candis_ov5640.h"
#include "candis_camera_sensor.h"
#include "esp_check.h"
#include <string.h>

/* OV5640 product specification v2.03, sections 2.8, 4.6, 7.4, 7.5 and 7.26. */
enum {
    OV5640_PID = 0x5640,
    REG_GROUP = 0x3212,
    REG_GROUP_STATUS = 0x3213,
    GROUP_START = 0x00,
    GROUP_END = 0x10,
    GROUP_LAUNCH = 0xa0,
    GROUP_BUSY = (1 << 5) | (1 << 4),
    REG_EXPOSURE = 0x3500,
    REG_AEC_AGC = 0x3503,
    MANUAL_AEC = 1 << 0,
    MANUAL_AGC = 1 << 1,
    REG_GAIN = 0x350a,
    REG_VTS = 0x380e,
    REG_AWB_RED = 0x3400,
    REG_AWB_GREEN = 0x3402,
    REG_AWB_BLUE = 0x3404,
    REG_AWB_MODE = 0x3406,
    REG_ISP_CONTROL = 0x5001,
    REG_ISP_CONTROL_0 = 0x5000,
    ISP_GAMMA_ENABLE = 1 << 5,
    REG_GAMMA_CONTROL = 0x5480,
    REG_GAMMA_FIRST = 0x5481,
    AWB_ENABLE = 1 << 0,
    COLOR_MATRIX_ENABLE = 1 << 1,
    MANUAL_AWB = 1 << 0,
    SDE_ENABLE = 1 << 7,
    REG_SDE_CONTROL = 0x5580,
    REG_SDE_U = 0x5583,
    REG_SDE_V = 0x5584,
    REG_SDE_CONTRAST = 0x5586,
    REG_SDE_BRIGHTNESS = 0x5587,
    REG_SDE_OPTIONS = 0x5588,
    SDE_LUMA = 1 << 2,
    SDE_SATURATION = 1 << 1,
    SDE_FIXED_OUTPUT = (1 << 7) | (1 << 6) | (1 << 5) | (1 << 4) | (1 << 3),
    SDE_NEGATIVE_BRIGHTNESS = 1 << 3,
    SDE_MANUAL_SATURATION = 1 << 6,
    REG_CIP_CONTROL = 0x5308,
    CIP_MANUAL_SHARPNESS = 1 << 6,
    CIP_MANUAL_DENOISE = 1 << 4,
    REG_CMX_CONTROL = 0x5380,
    REG_CMX_FIRST = 0x5381,
    REG_SPECIAL_EFFECT = 0x5580,
    REG_SPECIAL_U = 0x5583,
    REG_SPECIAL_V = 0x5584,
    REG_ISP_CONTROL_3 = 0x5003,
};

/* Espressif esp32-camera OV5640 reference CMX saturation map, levels -4..+4. */
static const uint8_t s_cmx_saturation[9][11] = {
    {0x1d, 0x60, 0x03, 0x07, 0x48, 0x4f, 0x4b, 0x40, 0x0b, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x08, 0x54, 0x5c, 0x58, 0x4b, 0x0d, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x0a, 0x60, 0x6a, 0x64, 0x56, 0x0e, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x0b, 0x6c, 0x77, 0x70, 0x60, 0x10, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x0c, 0x78, 0x84, 0x7d, 0x6b, 0x12, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x0d, 0x84, 0x91, 0x8a, 0x76, 0x14, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x0e, 0x90, 0x9e, 0x96, 0x80, 0x16, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x10, 0x9c, 0xac, 0xa2, 0x8b, 0x17, 0x01, 0x98},
    {0x1d, 0x60, 0x03, 0x11, 0xa8, 0xb9, 0xaf, 0x96, 0x19, 0x01, 0x98},
};

static const uint8_t s_special_effects[7][4] = {
    {0x06, 0x40, 0x2c, 0x08}, {0x46, 0x40, 0x28, 0x08}, {0x1e, 0x80, 0x80, 0x08},
    {0x1e, 0x80, 0xc0, 0x08}, {0x1e, 0x60, 0x60, 0x08}, {0x1e, 0xa0, 0x40, 0x08},
    {0x1e, 0x40, 0xa0, 0x08},
};

/* Espressif esp32-camera OV5640 reference default and alternate gamma tables. */
static const uint8_t s_gamma_reference[2][16] = {
    {0x08, 0x14, 0x28, 0x51, 0x65, 0x71, 0x7d, 0x87, 0x91, 0x9a, 0xaa, 0xb8, 0xcd, 0xdd, 0xea, 0x1d},
    {0x00, 0x1e, 0x3b, 0x58, 0x66, 0x71, 0x7d, 0x83, 0x8f, 0x98, 0xa6, 0xb8, 0xca, 0xd7, 0xe3, 0x1d},
};

typedef struct {
    uint16_t address;
    uint8_t value;
} register_write_t;

static const char *TAG = "candis_ov5640";
static bool s_group_open;

esp_err_t candis_ov5640_end_pending_group(void)
{
    if (!s_group_open) {
        return ESP_OK;
    }
    const esp_err_t err = candis_camera_sensor_write_register(REG_GROUP, GROUP_END);
    if (err == ESP_OK) {
        s_group_open = false;
    }
    return err;
}

static esp_err_t verify_sensor(void)
{
    ESP_RETURN_ON_ERROR(candis_ov5640_end_pending_group(), TAG, "recover control group");
    esp_cam_sensor_id_t id;
    ESP_RETURN_ON_ERROR(candis_camera_read_sensor_id(&id), TAG, "read sensor ID");
    return id.pid == OV5640_PID ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t read_word(uint16_t address, uint8_t high_mask, uint16_t *word)
{
    uint8_t high, low;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(address, &high), TAG, "read high byte");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(address + 1, &low), TAG, "read low byte");
    *word = ((uint16_t)(high & high_mask) << 8) | low;
    return ESP_OK;
}

static esp_err_t read_modes(uint8_t *aec_agc, uint8_t *awb)
{
    uint8_t isp;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_ISP_CONTROL, &isp), TAG, "read ISP mode");
    ESP_RETURN_ON_FALSE(isp & AWB_ENABLE, ESP_ERR_NOT_SUPPORTED, TAG, "AWB block disabled");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_AEC_AGC, aec_agc), TAG, "read AEC and AGC modes");
    return candis_camera_sensor_read_register(REG_AWB_MODE, awb);
}

esp_err_t candis_ov5640_get_controls(candis_ov5640_controls_t *controls, uint16_t *frame_lines)
{
    if (!controls) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");

    uint8_t aec_agc, awb, exposure_low;
    uint16_t exposure_high, vts;
    candis_ov5640_controls_t result;
    ESP_RETURN_ON_ERROR(read_modes(&aec_agc, &awb), TAG, "read control modes");
    ESP_RETURN_ON_ERROR(read_word(REG_EXPOSURE, 0x0f, &exposure_high), TAG, "read exposure");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_EXPOSURE + 2, &exposure_low), TAG, "read exposure tail");
    ESP_RETURN_ON_ERROR(read_word(REG_GAIN, 0x03, &result.gain_q4), TAG, "read analog gain");
    ESP_RETURN_ON_ERROR(read_word(REG_AWB_RED, 0x0f, &result.red_gain_q10), TAG, "read red gain");
    ESP_RETURN_ON_ERROR(read_word(REG_AWB_GREEN, 0x0f, &result.green_gain_q10), TAG, "read green gain");
    ESP_RETURN_ON_ERROR(read_word(REG_AWB_BLUE, 0x0f, &result.blue_gain_q10), TAG, "read blue gain");
    if (frame_lines) {
        ESP_RETURN_ON_ERROR(read_word(REG_VTS, 0xff, &vts), TAG, "read frame length");
        *frame_lines = vts;
    }
    result.exposure_lines = (exposure_high << 4) | (exposure_low >> 4);
    result.auto_exposure = !(aec_agc & MANUAL_AEC);
    result.auto_gain = !(aec_agc & MANUAL_AGC);
    result.auto_white_balance = !(awb & MANUAL_AWB);
    *controls = result;
    return ESP_OK;
}

static esp_err_t queue_word(register_write_t *writes, size_t *count,
                            uint16_t address, uint8_t high_mask, uint16_t value)
{
    uint8_t high;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(address, &high), TAG, "preserve register bits");
    writes[(*count)++] = (register_write_t){address, (high & ~high_mask) | ((value >> 8) & high_mask)};
    writes[(*count)++] = (register_write_t){address + 1, value & 0xff};
    return ESP_OK;
}

static bool valid_awb_gain(uint16_t gain)
{
    return gain > 0 && gain <= 0xfff;
}

static esp_err_t apply_group(const register_write_t *writes, size_t count)
{
    uint8_t status;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_GROUP_STATUS, &status), TAG, "read group status");
    ESP_RETURN_ON_FALSE(!(status & GROUP_BUSY), ESP_ERR_INVALID_STATE, TAG, "sensor group is busy");
    /* END without LAUNCH discards the pending update. A failed SCCB transfer
     * can still reach the sensor, so issue END even when START reported failure. */
    s_group_open = true;
    esp_err_t err = candis_camera_sensor_write_register(REG_GROUP, GROUP_START);
    for (size_t i = 0; err == ESP_OK && i < count; ++i) {
        err = candis_camera_sensor_write_register(writes[i].address, writes[i].value);
    }
    const esp_err_t end = candis_ov5640_end_pending_group();
    if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(end);
        return err;
    }
    ESP_RETURN_ON_ERROR(end, TAG, "end control group");
    return candis_camera_sensor_write_register(REG_GROUP, GROUP_LAUNCH);
}

esp_err_t candis_ov5640_set_controls(const candis_ov5640_controls_t *controls)
{
    if (!controls || (!controls->auto_exposure && !controls->exposure_lines) ||
        (!controls->auto_gain && (controls->gain_q4 < 16 || controls->gain_q4 > 0x3ff)) ||
        (!controls->auto_white_balance && (!valid_awb_gain(controls->red_gain_q10) ||
                                           !valid_awb_gain(controls->green_gain_q10) ||
                                           !valid_awb_gain(controls->blue_gain_q10)))) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");

    register_write_t writes[13];
    size_t count = 0;
    uint8_t aec_agc, awb;
    ESP_RETURN_ON_ERROR(read_modes(&aec_agc, &awb), TAG, "read existing control modes");
    if (!controls->auto_exposure) {
        uint16_t vts;
        ESP_RETURN_ON_ERROR(read_word(REG_VTS, 0xff, &vts), TAG, "read frame length");
        ESP_RETURN_ON_FALSE(controls->exposure_lines < vts, ESP_ERR_INVALID_ARG, TAG, "exposure exceeds frame");
        ESP_RETURN_ON_ERROR(queue_word(writes, &count, REG_EXPOSURE, 0x0f, controls->exposure_lines >> 4),
                            TAG, "set high exposure bits");
        writes[count++] = (register_write_t){REG_EXPOSURE + 2, (controls->exposure_lines & 0xf) << 4};
    }
    if (!controls->auto_gain) {
        ESP_RETURN_ON_ERROR(queue_word(writes, &count, REG_GAIN, 0x03, controls->gain_q4), TAG, "set analog gain");
    }
    if (!controls->auto_white_balance) {
        ESP_RETURN_ON_ERROR(queue_word(writes, &count, REG_AWB_RED, 0x0f, controls->red_gain_q10), TAG, "set red gain");
        ESP_RETURN_ON_ERROR(queue_word(writes, &count, REG_AWB_GREEN, 0x0f, controls->green_gain_q10), TAG, "set green gain");
        ESP_RETURN_ON_ERROR(queue_word(writes, &count, REG_AWB_BLUE, 0x0f, controls->blue_gain_q10), TAG, "set blue gain");
    }
    aec_agc &= ~(MANUAL_AEC | MANUAL_AGC);
    aec_agc |= controls->auto_exposure ? 0 : MANUAL_AEC;
    aec_agc |= controls->auto_gain ? 0 : MANUAL_AGC;
    awb = (awb & ~MANUAL_AWB) | (controls->auto_white_balance ? 0 : MANUAL_AWB);
    writes[count++] = (register_write_t){REG_AEC_AGC, aec_agc};
    writes[count++] = (register_write_t){REG_AWB_MODE, awb};
    return apply_group(writes, count);
}

esp_err_t candis_ov5640_set_ae_level(int8_t level)
{
    if (level < -5 || level > 5) {
        return ESP_ERR_INVALID_ARG;
    }
    candis_ov5640_controls_t controls;
    ESP_RETURN_ON_ERROR(candis_ov5640_get_controls(&controls, NULL), TAG, "read exposure mode");
    ESP_RETURN_ON_FALSE(controls.auto_exposure, ESP_ERR_INVALID_STATE, TAG, "AE target requires automatic exposure");

    /* Espressif's OV5640 reference maps +/-5 to a bounded target around 5..105. */
    const unsigned target = (level + 5) * 10 + 5;
    const uint8_t low = target * 23 / 25;
    const uint8_t high = target * 27 / 25;
    const uint8_t fast_low = low >> 1;
    const unsigned fast = high * 2U > UINT8_MAX ? UINT8_MAX : high * 2U;
    const register_write_t writes[] = {
        {0x3a0f, high}, {0x3a10, low}, {0x3a1b, high}, {0x3a1e, low},
        {0x3a11, fast}, {0x3a1f, fast_low},
    };
    return apply_group(writes, sizeof(writes) / sizeof(writes[0]));
}

esp_err_t candis_ov5640_set_white_balance_preset(candis_ov5640_wb_preset_t preset)
{
    static const uint16_t gains[][3] = {
        [CANDIS_OV5640_WB_AUTO] = {0, 0, 0},
        [CANDIS_OV5640_WB_SUNNY] = {0x5e0, 0x410, 0x540},
        [CANDIS_OV5640_WB_CLOUDY] = {0x650, 0x410, 0x4f0},
        [CANDIS_OV5640_WB_OFFICE] = {0x520, 0x410, 0x660},
        [CANDIS_OV5640_WB_HOME] = {0x420, 0x3f0, 0x710},
    };
    if ((unsigned)preset >= sizeof(gains) / sizeof(gains[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    candis_ov5640_controls_t controls;
    ESP_RETURN_ON_ERROR(candis_ov5640_get_controls(&controls, NULL), TAG, "read current controls");
    controls.auto_white_balance = preset == CANDIS_OV5640_WB_AUTO;
    if (preset != CANDIS_OV5640_WB_AUTO) {
        controls.red_gain_q10 = gains[preset][0];
        controls.green_gain_q10 = gains[preset][1];
        controls.blue_gain_q10 = gains[preset][2];
    }
    return candis_ov5640_set_controls(&controls);
}

static esp_err_t read_sde_mode(uint8_t *control, uint8_t *options)
{
    uint8_t isp;
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_ISP_CONTROL, &isp), TAG, "read ISP mode");
    ESP_RETURN_ON_FALSE(isp & SDE_ENABLE, ESP_ERR_NOT_SUPPORTED, TAG, "SDE block disabled");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SDE_CONTROL, control), TAG, "read SDE mode");
    ESP_RETURN_ON_FALSE(!(*control & SDE_FIXED_OUTPUT), ESP_ERR_NOT_SUPPORTED, TAG, "fixed output effect active");
    return candis_camera_sensor_read_register(REG_SDE_OPTIONS, options);
}

esp_err_t candis_ov5640_get_isp(candis_ov5640_isp_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t control, options, magnitude;
    candis_ov5640_isp_t result;
    ESP_RETURN_ON_ERROR(read_sde_mode(&control, &options), TAG, "read ISP adjustment mode");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SDE_BRIGHTNESS, &magnitude), TAG, "read brightness");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SDE_CONTRAST, &result.contrast), TAG, "read contrast");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SDE_U, &result.saturation_u), TAG, "read saturation U");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SDE_V, &result.saturation_v), TAG, "read saturation V");
    result.brightness = options & SDE_NEGATIVE_BRIGHTNESS ? -(int16_t)magnitude : magnitude;
    result.luma_enabled = (control & SDE_LUMA) != 0;
    result.saturation_enabled = (control & SDE_SATURATION) != 0;
    result.manual_saturation = (options & SDE_MANUAL_SATURATION) != 0;
    *settings = result;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_isp(const candis_ov5640_isp_t *settings)
{
    if (!settings || settings->brightness < -255 || settings->brightness > 255 ||
        (settings->saturation_enabled && !settings->manual_saturation &&
         settings->saturation_u < settings->saturation_v)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t control, options;
    ESP_RETURN_ON_ERROR(read_sde_mode(&control, &options), TAG, "read ISP adjustment mode");
    control &= ~(SDE_LUMA | SDE_SATURATION);
    control |= settings->luma_enabled ? SDE_LUMA : 0;
    control |= settings->saturation_enabled ? SDE_SATURATION : 0;
    options &= ~(SDE_NEGATIVE_BRIGHTNESS | SDE_MANUAL_SATURATION);
    options |= settings->brightness < 0 ? SDE_NEGATIVE_BRIGHTNESS : 0;
    options |= settings->manual_saturation ? SDE_MANUAL_SATURATION : 0;
    const int16_t brightness = settings->brightness;
    const register_write_t writes[] = {
        {REG_SDE_CONTROL, control},
        {REG_SDE_U, settings->saturation_u},
        {REG_SDE_V, settings->saturation_v},
        {REG_SDE_CONTRAST, settings->contrast},
        {REG_SDE_BRIGHTNESS, brightness < 0 ? -brightness : brightness},
        {REG_SDE_OPTIONS, options},
    };
    return apply_group(writes, sizeof(writes) / sizeof(writes[0]));
}

esp_err_t candis_ov5640_get_cip(candis_ov5640_cip_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    uint8_t control;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_CIP_CONTROL, &control), TAG, "read CIP control");
    candis_ov5640_cip_t result = {
        .auto_sharpness = !(control & CIP_MANUAL_SHARPNESS),
    };
    if (!result.auto_sharpness) {
        uint8_t first, second;
        ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(0x5302, &first), TAG, "read sharpness offset");
        ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(0x5303, &second), TAG, "read sharpness offset");
        ESP_RETURN_ON_FALSE(first == second + 1 && second <= 48 && second % 8 == 0,
                            ESP_ERR_NOT_SUPPORTED, TAG, "manual sharpness mapping is not recognized");
        result.sharpness = second / 8 - 3;
    }
    if (control & CIP_MANUAL_DENOISE) {
        uint8_t offset;
        ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(0x5306, &offset), TAG, "read denoise level");
        ESP_RETURN_ON_FALSE(offset <= 28 && offset % 4 == 0,
                            ESP_ERR_NOT_SUPPORTED, TAG, "manual denoise mapping is not recognized");
        result.denoise = offset / 4 + 1;
    }
    *settings = result;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_cip(const candis_ov5640_cip_t *settings)
{
    if (!settings || settings->sharpness < -3 || settings->sharpness > 3 || settings->denoise > 8) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    uint8_t control;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_CIP_CONTROL, &control), TAG, "read CIP control");
    if (settings->auto_sharpness) {
        control &= ~CIP_MANUAL_SHARPNESS;
    } else {
        control |= CIP_MANUAL_SHARPNESS;
    }
    if (settings->denoise) {
        control |= CIP_MANUAL_DENOISE;
    } else {
        control &= ~CIP_MANUAL_DENOISE;
    }
    register_write_t writes[10];
    size_t count = 0;
    if (!settings->auto_sharpness) {
        const uint8_t offset = (settings->sharpness + 3) * 8;
        const register_write_t sharpness[] = {
            {0x5300, 0x10}, {0x5301, 0x10}, {0x5302, offset + 1}, {0x5303, offset},
            {0x5309, 0x10}, {0x530a, 0x10}, {0x530b, 0x04}, {0x530c, 0x06},
        };
        for (size_t i = 0; i < sizeof(sharpness) / sizeof(sharpness[0]); ++i) {
            writes[count++] = sharpness[i];
        }
    }
    if (settings->denoise) {
        writes[count++] = (register_write_t){0x5306, (settings->denoise - 1) * 4};
    }
    writes[count++] = (register_write_t){REG_CIP_CONTROL, control};
    return apply_group(writes, count);
}

esp_err_t candis_ov5640_set_color_effect(candis_ov5640_effect_t effect)
{
    if ((unsigned)effect >= sizeof(s_special_effects) / sizeof(s_special_effects[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    const uint8_t *values = s_special_effects[effect];
    const register_write_t writes[] = {
        {REG_SPECIAL_EFFECT, values[0]}, {REG_SPECIAL_U, values[1]},
        {REG_SPECIAL_V, values[2]}, {REG_ISP_CONTROL_3, values[3]},
    };
    return apply_group(writes, sizeof(writes) / sizeof(writes[0]));
}

esp_err_t candis_ov5640_set_cmx_saturation(int8_t level)
{
    if (level < -4 || level > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    uint8_t cmx, sde, isp;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_CMX_CONTROL, &cmx), TAG, "read color matrix");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_SPECIAL_EFFECT, &sde), TAG, "read SDE mode");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_ISP_CONTROL, &isp), TAG, "read ISP matrix mode");
    ESP_RETURN_ON_FALSE(isp & COLOR_MATRIX_ENABLE, ESP_ERR_NOT_SUPPORTED, TAG, "color matrix disabled");
    ESP_RETURN_ON_FALSE(!(sde & SDE_FIXED_OUTPUT), ESP_ERR_NOT_SUPPORTED, TAG, "fixed SDE effect active");
    const uint8_t *values = s_cmx_saturation[level + 4];
    register_write_t writes[12];
    writes[0] = (register_write_t){REG_CMX_CONTROL, cmx};
    for (size_t i = 0; i < 11; ++i) {
        writes[i + 1] = (register_write_t){REG_CMX_FIRST + i, values[i]};
    }
    return apply_group(writes, sizeof(writes) / sizeof(writes[0]));
}

esp_err_t candis_ov5640_get_gamma(candis_ov5640_gamma_t *gamma)
{
    if (!gamma) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    uint8_t enable, control;
    candis_ov5640_gamma_t result;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_ISP_CONTROL_0, &enable), TAG, "read gamma enable");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_GAMMA_CONTROL, &control), TAG, "read gamma control");
    result.enabled = (enable & ISP_GAMMA_ENABLE) != 0;
    for (size_t i = 0; i < sizeof(result.points); ++i) {
        ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_GAMMA_FIRST + i, &result.points[i]),
                            TAG, "read gamma curve");
    }
    *gamma = result;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_gamma(const candis_ov5640_gamma_t *gamma)
{
    if (!gamma) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The last reference-table entry is a slope code, not a curve point. */
    for (size_t i = 1; i < sizeof(gamma->points) - 1; ++i) {
        if (gamma->points[i] < gamma->points[i - 1]) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    ESP_RETURN_ON_ERROR(verify_sensor(), TAG, "require OV5640");
    uint8_t isp, control;
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_ISP_CONTROL_0, &isp), TAG, "read gamma enable");
    ESP_RETURN_ON_ERROR(candis_camera_sensor_read_register(REG_GAMMA_CONTROL, &control), TAG, "read gamma control");
    register_write_t writes[18];
    size_t count = 0;
    isp = (isp & ~ISP_GAMMA_ENABLE) | (gamma->enabled ? ISP_GAMMA_ENABLE : 0);
    writes[count++] = (register_write_t){REG_ISP_CONTROL_0, isp};
    writes[count++] = (register_write_t){REG_GAMMA_CONTROL, control | 0x01};
    for (size_t i = 0; i < sizeof(gamma->points); ++i) {
        writes[count++] = (register_write_t){REG_GAMMA_FIRST + i, gamma->points[i]};
    }
    return apply_group(writes, count);
}

esp_err_t candis_ov5640_set_gamma_preset(unsigned preset)
{
    if (preset >= sizeof(s_gamma_reference) / sizeof(s_gamma_reference[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    candis_ov5640_gamma_t gamma = {.enabled = true};
    memcpy(gamma.points, s_gamma_reference[preset], sizeof(gamma.points));
    return candis_ov5640_set_gamma(&gamma);
}
