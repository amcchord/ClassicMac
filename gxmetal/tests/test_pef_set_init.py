#!/usr/bin/env python3
"""Unit tests for the narrowly scoped MakePEF shared-library fixup."""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import pef_set_init  # noqa: E402


def minimal_pef(main_section=1, main_offset=0x3C, init_section=-1):
    data = bytearray(40 + 28 + 56)
    data[:12] = b"Joy!peffpwpc"
    struct.pack_into(">I", data, 12, 1)
    struct.pack_into(">H", data, 32, 1)
    section = 40
    struct.pack_into(">I", data, section + 16, 56)
    struct.pack_into(">I", data, section + 20, 68)
    data[section + 24] = 4
    struct.pack_into(">iIiI", data, 68,
                     main_section, main_offset, init_section, 0)
    struct.pack_into(">iI", data, 84, -1, 0)
    return data


class PEFSetInitTests(unittest.TestCase):
    def test_converts_main_descriptor_to_init_descriptor(self):
        with tempfile.NamedTemporaryFile() as output:
            output.write(minimal_pef())
            output.flush()
            pef_set_init.set_init(output.name)
            with open(output.name, "rb") as source:
                info = pef_set_init.describe(source.read())
        self.assertEqual(info["main_section"], -1)
        self.assertEqual(info["main_offset"], 0)
        self.assertEqual(info["init_section"], 1)
        self.assertEqual(info["init_offset"], 0x3C)

    def test_rejects_missing_entry_descriptor(self):
        with tempfile.NamedTemporaryFile() as output:
            output.write(minimal_pef(-1, 0xFFFFFFFF))
            output.flush()
            with self.assertRaisesRegex(ValueError, "no valid XCOFF entry"):
                pef_set_init.set_init(output.name)

    def test_rejects_non_pef_input(self):
        with self.assertRaisesRegex(ValueError, "not a PowerPC PEF"):
            pef_set_init.describe(b"not a pef")


if __name__ == "__main__":
    unittest.main()
