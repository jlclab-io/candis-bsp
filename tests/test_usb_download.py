# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
import unittest


HEADERS = Path(__file__).resolve().parents[1] / 'examples/common/candis_example_usb'

TEST_SOURCE = """
#include <assert.h>
#include "candis_usb_download.h"

int main(void)
{
    candis_usb_download_sequence_t state = {0};
    assert(!candis_usb_download_sequence(&state, true, true, 0));
    assert(!candis_usb_download_sequence(&state, false, false, 100000));

    /* An ordinary attach or a short phase must not enter ROM. */
    assert(!candis_usb_download_sequence(&state, false, true, 200000));
    assert(!candis_usb_download_sequence(&state, true, false, 210000));
    assert(!candis_usb_download_sequence(&state, false, false, 220000));

    /* A valid swap alone is insufficient, even if the host later goes idle. */
    assert(!candis_usb_download_sequence(&state, false, true, 300000));
    assert(!candis_usb_download_sequence(&state, true, false, 400000));
    assert(!candis_usb_download_sequence(&state, false, false, 1000000));

    /* Linux may report both lines asserted while swapping them. */
    assert(!candis_usb_download_sequence(&state, false, true, 2000000));
    assert(!candis_usb_download_sequence(&state, true, true, 2100000));
    assert(!candis_usb_download_sequence(&state, true, false, 2101000));
    assert(candis_usb_download_sequence(&state, false, false, 2150000));
    assert(!candis_usb_download_sequence(&state, false, false, 2160000));
    return 0;
}
"""


class UsbDownloadSequenceTest(unittest.TestCase):
    def test_only_complete_download_sequence_is_accepted(self):
        with TemporaryDirectory() as work:
            source = Path(work) / 'test_usb_download.c'
            executable = Path(work) / 'test_usb_download'
            source.write_text(TEST_SOURCE)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', f'-I{HEADERS}',
                            str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
