#!/usr/bin/env python3
"""Tests for deterministic MacBinary Finder-flag updates."""

import binascii
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import set_macbinary_flags  # noqa: E402


def header(flags=0):
    data = bytearray(128)
    data[1] = 1
    data[2] = ord("X")
    data[65:69] = b"shlb"
    data[69:73] = b"tnsl"
    data[73] = (flags >> 8) & 0xFF
    data[101] = flags & 0xFF
    data[122] = 129
    data[123] = 129
    data[124:126] = binascii.crc_hqx(data[:124], 0).to_bytes(2, "big")
    return data


class MacBinaryFlagsTest(unittest.TestCase):
    def test_sets_custom_icon_and_clears_init_flags(self):
        data = header(0x0180)
        flags = set_macbinary_flags.update_flags(data, 0x0400, 0x0180)
        self.assertEqual(flags, 0x0400)
        self.assertEqual((data[73] << 8) | data[101], 0x0400)
        self.assertEqual(
            int.from_bytes(data[124:126], "big"),
            binascii.crc_hqx(data[:124], 0),
        )

    def test_preserves_unrelated_flags(self):
        data = header(0x000E)
        flags = set_macbinary_flags.update_flags(data, 0x2000, 0x0100)
        self.assertEqual(flags, 0x200E)

    def test_rejects_non_macbinary_data(self):
        with self.assertRaisesRegex(ValueError, "not a MacBinary"):
            set_macbinary_flags.update_flags(bytearray(12), 0, 0)


if __name__ == "__main__":
    unittest.main()
