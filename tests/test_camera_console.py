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
#include "bsp/candis_ov5640.h"
#include "candis_example_usb.h"
#include "control_console.h"

static const char *input;
static unsigned applied, isp_applied, reads, ae_updates, wb_updates;
static candis_ov5640_controls_t current;
static candis_ov5640_isp_t current_isp;
static int8_t ae_level;
static candis_ov5640_wb_preset_t wb_preset;
static candis_ov5640_effect_t color_effect;
static int8_t saturation;
static unsigned effect_updates, saturation_updates;
static candis_ov5640_cip_t current_cip;
static unsigned cip_updates;
static candis_ov5640_gamma_t current_gamma;
static unsigned gamma_updates;

bool candis_example_usb_read(char *byte)
{
    if (!*input) {
        return false;
    }
    *byte = *input++;
    return true;
}

esp_err_t candis_ov5640_set_controls(const candis_ov5640_controls_t *settings)
{
    ++applied;
    current = *settings;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_isp(const candis_ov5640_isp_t *settings)
{
    ++isp_applied;
    current_isp = *settings;
    return ESP_OK;
}

esp_err_t candis_ov5640_get_controls(candis_ov5640_controls_t *settings, uint16_t *vts)
{
    ++reads;
    *settings = current;
    *vts = 1000;
    return ESP_OK;
}

esp_err_t candis_ov5640_get_isp(candis_ov5640_isp_t *settings)
{
    *settings = current_isp;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_ae_level(int8_t value)
{
    ae_level = value;
    ++ae_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_white_balance_preset(candis_ov5640_wb_preset_t value)
{
    wb_preset = value;
    ++wb_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_cip(const candis_ov5640_cip_t *settings)
{
    current_cip = *settings;
    ++cip_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_color_effect(candis_ov5640_effect_t value)
{
    color_effect = value;
    ++effect_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_cmx_saturation(int8_t value)
{
    saturation = value;
    ++saturation_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_gamma(const candis_ov5640_gamma_t *value)
{
    current_gamma = *value;
    ++gamma_updates;
    return ESP_OK;
}

esp_err_t candis_ov5640_set_gamma_preset(unsigned preset)
{
    if (preset > 1) return ESP_ERR_INVALID_ARG;
    current_gamma.enabled = true;
    current_gamma.points[0] = preset ? 0 : 0x08;
    current_gamma.points[15] = 0x1d;
    ++gamma_updates;
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t code)
{
    assert(code == ESP_ERR_INVALID_ARG);
    return "ESP_ERR_INVALID_ARG";
}

int main(void)
{
    input = "MANUAL 15";
    assert(camera_control_console_poll() && applied == 0);
    input = "0 32 1024 1024 1024\r\nREAD\n";
    assert(camera_control_console_poll() && applied == 1 && reads == 0);
    assert(current.exposure_lines == 150 && current.gain_q4 == 32 && current.blue_gain_q10 == 1024);
    assert(camera_control_console_poll() && reads == 1);

    input = "MANUAL 65536 32 1024 1024 1024\nMANUAL 150 31x 1024 1024 1024\n";
    assert(camera_control_console_poll() && camera_control_console_poll() && applied == 1);
    input = "ISP -16 32 64 65\nISP -256 32 64 65\n";
    assert(camera_control_console_poll() && isp_applied == 1 && current_isp.brightness == -16);
    assert(camera_control_console_poll() && isp_applied == 1);

    input = "AELEVEL 5\nAELEVEL 6\nWB CLOUDY\nWB WARM\n";
    assert(camera_control_console_poll() && ae_level == 5 && ae_updates == 1);
    assert(camera_control_console_poll() && ae_level == 5 && ae_updates == 1);
    assert(camera_control_console_poll() && wb_preset == CANDIS_OV5640_WB_CLOUDY && wb_updates == 1);
    assert(camera_control_console_poll() && wb_updates == 1);

    input = "CIP AUTO 5\nCIP MANUAL -2 4\nCIP MANUAL 4 5\n";
    assert(camera_control_console_poll() && cip_updates == 1 && current_cip.auto_sharpness && current_cip.denoise == 5);
    assert(camera_control_console_poll() && cip_updates == 2 && !current_cip.auto_sharpness &&
           current_cip.sharpness == -2 && current_cip.denoise == 4);
    assert(camera_control_console_poll() && cip_updates == 2);

    input = "EFFECT SEPIA\nEFFECT SEPIA!\nSATURATION -4\nSATURATION 5\n";
    assert(camera_control_console_poll() && color_effect == CANDIS_OV5640_EFFECT_SEPIA && effect_updates == 1);
    assert(camera_control_console_poll() && effect_updates == 1);
    assert(camera_control_console_poll() && saturation == -4 && saturation_updates == 1);
    assert(camera_control_console_poll() && saturation_updates == 1);
    input = "GAMMA 1\nGAMMA 2\n";
    assert(camera_control_console_poll() && gamma_updates == 1 && current_gamma.enabled &&
           current_gamma.points[0] == 0 && current_gamma.points[15] == 0x1d);
    assert(camera_control_console_poll() && gamma_updates == 1);

    char oversized[100];
    memset(oversized, 'x', 85);
    memcpy(oversized + 85, "AUTO\nAUTO\nSTOP", 15);
    oversized[100 - 1] = '\0';
    input = oversized;
    assert(camera_control_console_poll() && applied == 1);
    assert(camera_control_console_poll() && applied == 2);
    assert(current.auto_exposure && current.auto_gain && current.auto_white_balance);
    assert(camera_control_console_poll());
    input = "\n";
    assert(!camera_control_console_poll());
}
"""


class CameraConsoleTest(unittest.TestCase):
    def test_commands_validation_and_bounded_input(self):
        repo = Path(__file__).resolve().parents[1]
        idf = Path(os.environ["IDF_PATH"])
        with TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "sdkconfig.h").touch()
            source = root / "test.c"
            source.write_text(SOURCE)
            binary = root / "test"
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                 f"-I{root}", f"-I{idf / 'components/esp_common/include'}",
                 f"-I{repo / 'examples/common/candis_example_usb/include'}",
                 f"-I{repo / 'components/candis-camera/include'}",
                 f"-I{repo / 'examples/camera/main'}", str(repo / "examples/camera/main/control_console.c"),
                 str(source), "-o", str(binary)],
                check=True,
            )
            subprocess.run([binary], check=True)
