# SPDX-License-Identifier: Apache-2.0
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


SOURCE = r"""
#include <assert.h>
#include "candis_gamepad_filter.h"

int main(void)
{
    candis_gamepad_filter_t state = {0};
    assert(candis_gamepad_filter_update(&state, 1, 0, 0, 10000) == 0);
    assert(state.stable == 1);
    candis_gamepad_filter_update(&state, 0, 0, 2000, 10000);
    candis_gamepad_filter_update(&state, 1, 0, 4000, 10000);
    candis_gamepad_filter_update(&state, 2, 0, 5000, 10000);
    candis_gamepad_filter_update(&state, 6, 0, 9000, 10000);
    candis_gamepad_filter_update(&state, 6, 0, 15000, 10000);
    assert(state.stable == 2);
    candis_gamepad_filter_update(&state, 6, 0, 19000, 10000);
    assert(state.stable == 6);
    candis_gamepad_filter_update(&state, 0, 0, 20000, 0);
    assert(state.stable == 0);

    assert(candis_gamepad_filter_update(&state, 0, 1, 21000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 0, 22000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 1, 23000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 3, 24000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 2, 25000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 0, 26000, 0) == 1);
    assert(candis_gamepad_filter_update(&state, 0, 2, 27000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 3, 28000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 1, 29000, 0) == 0);
    assert(candis_gamepad_filter_update(&state, 0, 0, 30000, 0) == -1);
    candis_gamepad_filter_update(&state, 0, 1, 31000, 0);
    assert(state.quarters == 1);
    assert(candis_gamepad_filter_update(&state, 0, 2, 32000, 0) == 0);
    assert(state.quarters == 0);
}
"""


class GamepadTest(unittest.TestCase):
    def test_button_debounce_and_encoder_phases(self):
        component = Path(__file__).resolve().parents[1] / "components/candis_gamepad"
        with TemporaryDirectory() as tmp:
            source = Path(tmp) / "gamepad.c"
            executable = Path(tmp) / "gamepad"
            source.write_text(SOURCE)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{component}",
                 str(source), "-o", str(executable)],
                check=True,
            )
            subprocess.run([executable], check=True)
