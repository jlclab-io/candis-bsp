# SPDX-License-Identifier: Apache-2.0
import ctypes
import os
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory
import unittest


ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "components/candis"
SIZE = 134
OK, INVALID_SIZE, NOT_SUPPORTED, INVALID_RESPONSE, INVALID_CRC = 0, 0x104, 0x106, 0x108, 0x109


def seal(image):
    # Generate CRC16-MODBUS using a byte lookup table independent of the C decoder.
    def table_entry(value):
        for _ in range(8):
            value = (value >> 1) ^ (0xA001 if value & 1 else 0)
        return value

    table = [table_entry(i) for i in range(256)]
    for begin, end in ((0, 0x34), (0x36, 0x3E), (0x40, 0x84)):
        crc = 0xFFFF
        for value in image[begin:end]:
            crc = (crc >> 8) ^ table[(crc ^ value) & 0xFF]
        struct.pack_into("<H", image, end, crc)
    return image


def descriptor(kind=0x30, board=1, revision=0x0100):
    image = bytearray(SIZE)
    image[:4] = b"ESP" + bytes([kind])
    struct.pack_into("<4H", image, 4, board, revision, 0x0100, 0xCA4E)
    image[0x14:0x34] = b"!" * 32
    struct.pack_into("<H", image, 0x40, 1)
    return seal(image)


class BoardInfo(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int), ("revision", ctypes.c_uint16),
                ("format", ctypes.c_uint16), ("name", ctypes.c_char * 33)]


class BaseboardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work = TemporaryDirectory()
        cls.addClassCleanup(work.cleanup)
        library = Path(work.name) / "libcandis.so"
        (Path(work.name) / "sdkconfig.h").write_text("#define CONFIG_COMPILER_OPTIMIZATION_PERF 0" + chr(10))
        idf = Path(os.environ["IDF_PATH"])
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Werror",
                        f"-I{work.name}",
                        f"-I{COMPONENT / 'include'}", f"-I{idf / 'components/esp_common/include'}",
                        str(COMPONENT / "candis_baseboard.c"), "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.candis_baseboard_decode.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(BoardInfo))
        cls.lib.candis_baseboard_decode.restype = ctypes.c_int

    def decode(self, image):
        info = BoardInfo(kind=99)
        result = self.lib.candis_baseboard_decode(bytes(image), len(image), ctypes.byref(info))
        if result != OK:
            self.assertEqual(info.kind, 99)
        return result, info

    def test_approved_hardware(self):
        for code, board, revision, kind in ((0x30, 1, 0x100, 1), (0x31, 2, 0x101, 2), (0x31, 2, 0x102, 2)):
            with self.subTest(code=code, revision=revision):
                result, info = self.decode(descriptor(code, board, revision))
                self.assertEqual(result, OK)
                self.assertEqual((info.kind, info.revision, info.format), (kind, revision, 1))
                self.assertEqual(info.name, b"!" * 32)

    def test_invalid_crc_by_section(self):
        for offset in (0x10, 0x36, 0x44):
            with self.subTest(offset=offset):
                image = descriptor()
                image[offset] ^= 1
                self.assertEqual(self.decode(image)[0], INVALID_CRC)

    def test_invalid_format_or_identity(self):
        self.assertEqual(self.decode(descriptor()[:-1])[0], INVALID_SIZE)
        self.assertEqual(self.decode(bytes(SIZE))[0], INVALID_RESPONSE)
        self.assertEqual(self.decode(descriptor(revision=0x101))[0], NOT_SUPPORTED)
        self.assertEqual(self.decode(descriptor(0x31, 2, 0x103))[0], NOT_SUPPORTED)
        for offset in (0x08, 0x0A, 0x0D, 0x40):
            with self.subTest(offset=offset):
                image = descriptor()
                image[offset] ^= 1
                self.assertEqual(self.decode(seal(image))[0], NOT_SUPPORTED)
        image = descriptor()
        image[0x42] = 65
        self.assertEqual(self.decode(seal(image))[0], INVALID_SIZE)


if __name__ == "__main__":
    unittest.main()
