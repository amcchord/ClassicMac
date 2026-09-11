#!/usr/bin/env python3
"""Focused regression checks for classic HFS+ wrapper reconstruction."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("prepare", Path(__file__).with_name("prepare-os9-template.py"))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class WrapperTests(unittest.TestCase):
    def fixture(self, path, wrapped):
        total = 16 * 1024 ** 2
        start = 64 * 512
        with path.open("wb") as file:
            header = bytearray(512)
            header[:2] = b"ER"
            struct.pack_into(">H", header, 2, 512)
            file.write(header)
            for index, kind in [(1, b"Apple_partition_map"), (2, b"Apple_HFS")]:
                entry = bytearray(512)
                entry[:2] = b"PM"
                struct.pack_into(">III", entry, 4, 2, 64, total // 512 - 64)
                entry[48:48 + len(kind)] = kind
                file.write(entry)
            file.truncate(total)
            if wrapped:
                wrapper = bytearray(512)
                wrapper[:2] = b"BD"
                struct.pack_into(">I", wrapper, 20, 4096)
                struct.pack_into(">H", wrapper, 28, 24)
                wrapper[124:126] = b"H+"
                struct.pack_into(">HH", wrapper, 126, 5, 2048)
                file.seek(start + 1024)
                file.write(wrapper)
                file.seek(65536 + 4096)
                file.write(b"PRIVATE SOURCE FILE MUST NOT BE COPIED")
            else:
                header = bytearray(512)
                header[:4] = b"H+\0\4"
                struct.pack_into(">II", header, 40, 4096, 1000)
                struct.pack_into(">I", header, 80, 21)
                struct.pack_into(">I", header, 92, 21)
                file.seek(start + 1024)
                file.write(header)
                file.seek(start + 4096)
                file.write(b"FRESH APPROVED SYSTEM FILE")
        return start

    def testAlternateHeaderUsesEmbeddedExtentEndAndSourceDataIsExcluded(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = [Path(directory) / name for name in ("source.img", "fresh.img")]
            self.fixture(source, wrapped=True)
            payload_start = self.fixture(output, wrapped=False)
            before = hashlib.sha256(source.read_bytes()).digest()
            with output.open("rb") as file:
                file.seek(payload_start + 1024)
                expected_header = file.read(512)
            prepare.retain_classic_wrapper(source, output)
            embedded_start = 65536
            embedded_end = embedded_start + 2048 * 4096
            with output.open("rb") as file:
                for offset in (embedded_start + 1024, embedded_end - 1024):
                    file.seek(offset)
                    self.assertEqual(file.read(512), expected_header)
                file.seek(embedded_start + 4096)
                marker = b"FRESH APPROVED SYSTEM FILE"
                self.assertEqual(file.read(len(marker)), marker)
            self.assertNotIn(b"PRIVATE SOURCE FILE", output.read_bytes())
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)


if __name__ == "__main__":
    unittest.main()
