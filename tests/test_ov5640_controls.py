# SPDX-License-Identifier: Apache-2.0
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


SOURCE = r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "bsp/candis_camera.h"
#include "bsp/candis_ov5640.h"
#include "candis_camera_sensor.h"

static uint8_t regs[0x6000];
static struct { uint16_t address; uint8_t value; } pending[32];
static size_t count;
static bool hold, launched, fail_end;
static uint16_t pid = 0x5640, fail_address;
static unsigned writes;

esp_err_t candis_camera_read_sensor_id(esp_cam_sensor_id_t *id)
{
    id->pid = pid;
    return ESP_OK;
}

esp_err_t candis_camera_sensor_read_register(uint16_t address, uint8_t *value)
{
    assert(address < sizeof(regs) && !hold);
    *value = address == 0x3213 && launched ? 0x10 : regs[address];
    return ESP_OK;
}

esp_err_t candis_camera_sensor_write_register(uint16_t address, uint8_t value)
{
    ++writes;
    if (address == fail_address) {
        return ESP_ERR_TIMEOUT;
    }
    if (address == 0x3212) {
        switch (value) {
        case 0x00:
            assert(!hold && !launched);
            hold = true;
            count = 0;
            break;
        case 0x10:
            if (fail_end) {
                return ESP_ERR_TIMEOUT;
            }
            hold = false;
            break;
        case 0xa0:
            assert(!hold && !launched);
            launched = true;
            break;
        default:
            assert(false);
        }
        return ESP_OK;
    }
    assert(hold && count < sizeof(pending) / sizeof(pending[0]));
    pending[count].address = address;
    pending[count].value = value;
    ++count;
    return ESP_OK;
}

static void next_frame(void)
{
    assert(launched && !hold);
    for (size_t i = 0; i < count; ++i) {
        regs[pending[i].address] = pending[i].value;
    }
    launched = false;
}

int main(void)
{
    regs[0x380e] = 0x03;
    regs[0x380f] = 0xe8;
    regs[0x5001] = 0xa3;
    regs[0x3503] = 0x80;
    regs[0x3406] = 0xa0;
    regs[0x3500] = 0x90;
    regs[0x350a] = 0xc0;
    regs[0x3400] = 0x70;
    candis_ov5640_controls_t actual;
    uint16_t frame_lines;
    assert(candis_ov5640_get_controls(&actual, &frame_lines) == ESP_OK);
    assert(actual.auto_exposure && actual.auto_gain && actual.auto_white_balance && frame_lines == 1000);
    assert(candis_ov5640_set_ae_level(6) == ESP_ERR_INVALID_ARG && writes == 0);
    assert(candis_ov5640_set_ae_level(0) == ESP_OK && launched && count == 6);
    assert(pending[0].address == 0x3a0f && pending[0].value == 59);
    assert(pending[1].address == 0x3a10 && pending[1].value == 50);
    assert(pending[4].address == 0x3a11 && pending[4].value == 118);
    next_frame();
    assert(regs[0x3a0f] == 59 && regs[0x3a10] == 50 && regs[0x3a11] == 118);
    assert(candis_ov5640_set_white_balance_preset(CANDIS_OV5640_WB_SUNNY) == ESP_OK &&
           launched && count == 8);
    next_frame();
    assert(regs[0x3406] == 0xa1 && regs[0x3400] == 0x75 && regs[0x3401] == 0xe0);
    assert(regs[0x3402] == 4 && regs[0x3403] == 0x10 && regs[0x3404] == 5 && regs[0x3405] == 0x40);
    assert(candis_ov5640_get_controls(&actual, NULL) == ESP_OK && !actual.auto_white_balance);
    assert(candis_ov5640_set_ae_level(0) == ESP_OK);
    next_frame();

    candis_ov5640_controls_t manual = {
        .exposure_lines = 0x1a5, .gain_q4 = 0x135,
        .red_gain_q10 = 0x66a, .green_gain_q10 = 0x402, .blue_gain_q10 = 0x731,
    };
    pid = 0x2642;
    unsigned before_invalid = writes;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_NOT_SUPPORTED && writes == before_invalid);
    pid = 0x5640;
    manual.exposure_lines = 1000;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_INVALID_ARG && writes == before_invalid);
    manual.exposure_lines = 0x1a5;
    manual.gain_q4 = 1024;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_INVALID_ARG && writes == before_invalid);
    manual.gain_q4 = 0x135;

    assert(candis_ov5640_set_controls(&manual) == ESP_OK && launched && count == 13);
    assert(regs[0x3503] == 0x80 && regs[0x3406] == 0xa1);
    const unsigned before_busy = writes;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_INVALID_STATE && writes == before_busy);
    next_frame();
    assert(regs[0x3503] == 0x83 && regs[0x3406] == 0xa1);
    assert(regs[0x3500] == 0x90 && regs[0x3501] == 0x1a && regs[0x3502] == 0x50);
    assert(regs[0x350a] == 0xc1 && regs[0x3400] == 0x76 && regs[0x5001] == 0xa3);
    assert(candis_ov5640_get_controls(&actual, NULL) == ESP_OK);
    assert(!actual.auto_exposure && !actual.auto_gain && !actual.auto_white_balance);
    assert(actual.exposure_lines == manual.exposure_lines && actual.gain_q4 == manual.gain_q4);
    assert(actual.red_gain_q10 == manual.red_gain_q10 && actual.green_gain_q10 == manual.green_gain_q10 &&
           actual.blue_gain_q10 == manual.blue_gain_q10);
    const unsigned before_manual_ae = writes;
    assert(candis_ov5640_set_ae_level(0) == ESP_ERR_INVALID_STATE && writes == before_manual_ae);

    fail_address = 0x3402;
    manual.exposure_lines = 200;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_TIMEOUT && !launched && !hold);
    assert(regs[0x3501] == 0x1a);
    fail_address = 0;
    fail_end = true;
    assert(candis_ov5640_set_controls(&manual) == ESP_ERR_TIMEOUT && hold && !launched);
    assert(candis_ov5640_end_pending_group() == ESP_ERR_TIMEOUT);
    fail_end = false;
    assert(candis_ov5640_end_pending_group() == ESP_OK && !hold && !launched);

    const candis_ov5640_controls_t automatic = {
        .auto_exposure = true, .auto_gain = true, .auto_white_balance = true,
    };
    assert(candis_ov5640_set_controls(&automatic) == ESP_OK && launched && count == 2);
    next_frame();
    assert(regs[0x3503] == 0x80 && regs[0x3406] == 0xa0 && regs[0x3501] == 0x1a);

    candis_ov5640_isp_t isp = {
        .luma_enabled = true, .saturation_enabled = true, .manual_saturation = true,
        .brightness = -48, .contrast = 0x28, .saturation_u = 0x50, .saturation_v = 0x60,
    };
    regs[0x5580] = 0x01;
    regs[0x5588] = 0x13;
    assert(candis_ov5640_set_isp(&isp) == ESP_OK && launched && count == 6);
    next_frame();
    assert(regs[0x5580] == 0x07 && regs[0x5588] == 0x5b);
    candis_ov5640_isp_t readback;
    assert(candis_ov5640_get_isp(&readback) == ESP_OK);
    assert(readback.luma_enabled && readback.saturation_enabled && readback.manual_saturation);
    assert(readback.brightness == -48 && readback.contrast == 0x28);
    assert(readback.saturation_u == 0x50 && readback.saturation_v == 0x60);
    const unsigned before_fixed = writes;
    regs[0x5580] |= 0x08;
    const esp_err_t fixed_result = candis_ov5640_set_isp(&isp);
    assert(fixed_result == ESP_ERR_NOT_SUPPORTED && writes == before_fixed);
    regs[0x5580] &= ~0x08;
    isp.manual_saturation = false;
    assert(candis_ov5640_set_isp(&isp) == ESP_ERR_INVALID_ARG && writes == before_fixed);
    isp.manual_saturation = true;
    regs[0x5580] |= 0x40;
    assert(candis_ov5640_set_isp(&isp) == ESP_ERR_NOT_SUPPORTED && writes == before_fixed);
    assert(candis_ov5640_set_cmx_saturation(0) == ESP_ERR_NOT_SUPPORTED && writes == before_fixed);
    regs[0x5580] &= ~0x40;

    regs[0x5308] = 0x25;
    candis_ov5640_cip_t cip = {.sharpness = 0, .denoise = 5};
    assert(candis_ov5640_set_cip(&cip) == ESP_OK && launched && count == 10);
    next_frame();
    assert(regs[0x5308] == 0x75 && regs[0x5302] == 0x19 && regs[0x5303] == 0x18 &&
           regs[0x5306] == 16);
    candis_ov5640_cip_t read_cip;
    assert(candis_ov5640_get_cip(&read_cip) == ESP_OK);
    assert(!read_cip.auto_sharpness && read_cip.sharpness == 0 && read_cip.denoise == 5);
    cip = (candis_ov5640_cip_t){.auto_sharpness = true};
    assert(candis_ov5640_set_cip(&cip) == ESP_OK && launched && count == 1);
    next_frame();
    assert(regs[0x5308] == 0x25 && regs[0x5306] == 16);
    assert(candis_ov5640_get_cip(&read_cip) == ESP_OK && read_cip.auto_sharpness && read_cip.denoise == 0);
    cip = (candis_ov5640_cip_t){.sharpness = 4};
    const unsigned before_invalid_cip = writes;
    assert(candis_ov5640_set_cip(&cip) == ESP_ERR_INVALID_ARG && writes == before_invalid_cip);

    const unsigned before_invalid_effect = writes;
    assert(candis_ov5640_set_color_effect((candis_ov5640_effect_t)7) == ESP_ERR_INVALID_ARG &&
           writes == before_invalid_effect);
    assert(candis_ov5640_set_color_effect(CANDIS_OV5640_EFFECT_SEPIA) == ESP_OK && launched && count == 4);
    next_frame();
    assert(regs[0x5580] == 0x1e && regs[0x5583] == 0x40 &&
           regs[0x5584] == 0xa0 && regs[0x5003] == 0x08);
    assert(candis_ov5640_set_color_effect(CANDIS_OV5640_EFFECT_NORMAL) == ESP_OK && launched && count == 4);
    next_frame();
    assert(regs[0x5580] == 0x06 && regs[0x5583] == 0x40 && regs[0x5584] == 0x2c);

    regs[0x5001] &= ~0x02;
    const unsigned before_matrix_disabled = writes;
    assert(candis_ov5640_set_cmx_saturation(0) == ESP_ERR_NOT_SUPPORTED &&
           writes == before_matrix_disabled);
    regs[0x5001] |= 0x02;
    regs[0x5380] = 0x20;
    assert(candis_ov5640_set_cmx_saturation(5) == ESP_ERR_INVALID_ARG);
    assert(candis_ov5640_set_cmx_saturation(-4) == ESP_OK && launched && count == 12);
    next_frame();
    assert(regs[0x5380] == 0x20 && regs[0x5381] == 0x1d && regs[0x5384] == 0x07 &&
           regs[0x538b] == 0x98);
    regs[0x5580] = 0x08;
    const unsigned before_conflict = writes;
    assert(candis_ov5640_set_cmx_saturation(0) == ESP_ERR_NOT_SUPPORTED && writes == before_conflict);
    regs[0x5580] = 0;

    candis_ov5640_gamma_t gamma = {.enabled = true};
    for (unsigned i = 0; i < sizeof(gamma.points); ++i) gamma.points[i] = i * 10;
    assert(candis_ov5640_set_gamma(&gamma) == ESP_OK && launched && count == 18);
    next_frame();
    assert(regs[0x5000] == 0x20 && regs[0x5480] == 0x01 && regs[0x5481] == 0 &&
           regs[0x5490] == 150);
    candis_ov5640_gamma_t read_gamma;
    assert(candis_ov5640_get_gamma(&read_gamma) == ESP_OK && read_gamma.enabled);
    for (unsigned i = 0; i < sizeof(gamma.points); ++i) assert(read_gamma.points[i] == gamma.points[i]);
    const uint8_t reference_gamma[] = {
        0x08, 0x14, 0x28, 0x51, 0x65, 0x71, 0x7d, 0x87,
        0x91, 0x9a, 0xaa, 0xb8, 0xcd, 0xdd, 0xea, 0x1d,
    };
    memcpy(gamma.points, reference_gamma, sizeof(reference_gamma));
    assert(candis_ov5640_set_gamma(&gamma) == ESP_OK && launched && count == 18);
    next_frame();
    for (unsigned i = 0; i < sizeof(gamma.points); ++i) assert(regs[0x5481 + i] == reference_gamma[i]);
    assert(candis_ov5640_set_gamma_preset(0) == ESP_OK && launched && count == 18);
    next_frame();
    for (unsigned i = 0; i < sizeof(gamma.points); ++i) assert(regs[0x5481 + i] == reference_gamma[i]);
    assert(candis_ov5640_set_gamma_preset(1) == ESP_OK && launched && count == 18);
    next_frame();
    assert(regs[0x5481] == 0x00 && regs[0x5482] == 0x1e && regs[0x5490] == 0x1d);
    const unsigned before_invalid_gamma_preset = writes;
    assert(candis_ov5640_set_gamma_preset(2) == ESP_ERR_INVALID_ARG &&
           writes == before_invalid_gamma_preset);
    gamma.points[7] = gamma.points[6] - 1;
    const unsigned before_gamma_error = writes;
    assert(candis_ov5640_set_gamma(&gamma) == ESP_ERR_INVALID_ARG && writes == before_gamma_error);
}
"""

PIXELS = r"""
#include <assert.h>
#include "camera_pixels.h"

int main(void)
{
    const uint8_t yuyv[] = {16, 128, 235, 128};
    const uint8_t uyvy[] = {128, 16, 128, 235};
    for (unsigned pixel = 0; pixel < 2; ++pixel) {
        const uint16_t expected = pixel ? 0xffff : 0;
        assert(camera_yuv_pixel(yuyv, true, pixel, true) == expected);
        assert(camera_yuv_pixel(uyvy, false, pixel, true) == expected);
    }
    const uint8_t red[] = {76, 85, 32, 255};
    const uint8_t blue[] = {255, 29, 107, 50};
    assert(camera_yuv_pixel(red, true, 0, false) == 0xf800);
    assert(camera_yuv_pixel(blue, false, 0, false) == 0x001f);
    assert(camera_yuv_to_rgb565(0, 128, 128, false) == 0);
    assert(camera_yuv_to_rgb565(255, 128, 128, false) == 0xffff);
}
"""


class Ov5640Test(unittest.TestCase):
    def test_group_controls_and_error_recovery(self):
        component = Path(__file__).resolve().parents[1] / "components/candis-camera"
        idf = Path(os.environ["IDF_PATH"]) / "components/esp_common/include"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sdkconfig.h").touch()
            (root / "bsp").mkdir()
            (root / "bsp/candis_camera.h").write_text(
                '#pragma once\n#include <stdint.h>\n#include <stddef.h>\n#include "esp_err.h"\n'
                'typedef struct { uint16_t pid; } esp_cam_sensor_id_t;\n'
                'esp_err_t candis_camera_read_sensor_id(esp_cam_sensor_id_t *id);\n'
            )
            (root / "esp_check.h").write_text(
                '#undef ESP_ERROR_CHECK_WITHOUT_ABORT\n'
                '#define ESP_ERROR_CHECK_WITHOUT_ABORT(call) ((void)(call))\n'
                '#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); '
                'esp_err_t code = (call); if (code != ESP_OK) return code; } while (0)\n'
                '#define ESP_RETURN_ON_FALSE(condition, error, tag, ...) do { (void)(tag); '
                'if (!(condition)) return error; } while (0)\n'
            )
            test = root / "fixture.c"
            test.write_text(SOURCE)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-fno-sanitize-recover=undefined", f"-I{root}", f"-I{idf}",
                 f"-I{component / 'include'}", f"-I{component / 'priv_include'}", str(component / "src/candis_ov5640.c"),
                 str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)

    def test_packed_yuv422_pixel_order_and_range(self):
        example = Path(__file__).resolve().parents[1] / "examples/camera/main"
        with TemporaryDirectory() as temp:
            root = Path(temp)
            test = root / "pixels.c"
            test.write_text(PIXELS)
            binary = root / "pixels"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 f"-I{example}", str(test), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)
