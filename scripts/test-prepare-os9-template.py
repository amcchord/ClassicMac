#!/usr/bin/env python3
"""Focused regression checks for sparse capacity and HFS+ boot reconstruction."""
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
    def fixture(self, path):
        total = 16 * 1024 ** 2
        start = 64 * 512
        partition_size = total - start - 17 * 512
        block_size, allocation_start = 4096, 24 * 512
        blocks = (partition_size - allocation_start - 1024) // block_size
        with path.open("wb") as file:
            header = bytearray(512)
            header[:2] = b"ER"
            struct.pack_into(">HI", header, 2, 512, total // 512)
            file.write(header)
            for index, kind, first, count in [
                (1, b"Apple_partition_map", 1, 63),
                (2, b"Apple_HFS", start // 512, partition_size // 512),
                (3, b"Apple_Free", (start + partition_size) // 512, 17),
            ]:
                entry = bytearray(512)
                entry[:2] = b"PM"
                struct.pack_into(">III", entry, 4, 3, first, count)
                entry[48:48 + len(kind)] = kind
                struct.pack_into(">I", entry, 84, count)
                file.write(entry)
            file.truncate(total)
            wrapper = bytearray(512)
            wrapper[:2] = b"BD"
            struct.pack_into(">H", wrapper, 18, blocks)
            for offset in (20, 24, 74, 78):
                struct.pack_into(">I", wrapper, offset, block_size)
            struct.pack_into(">H", wrapper, 28, allocation_start // 512)
            wrapper[124:126] = b"H+"
            struct.pack_into(">HH", wrapper, 126, 5, blocks - 5)
            struct.pack_into(">HH", wrapper, 150, 1, 1)
            file.seek(start + 1024)
            file.write(wrapper)
            catalog = bytearray(block_size)
            struct.pack_into(">H", catalog, 32, 512)
            for i, name in enumerate((b"Finder", b"System", b"Where_have_all_my_files_gone?", b"Desktop DB", b"Desktop DF"), 1):
                node, key = i * 512, i * 512 + 14
                catalog[node + 8] = 0xff
                struct.pack_into(">H", catalog, node + 10, 1)
                struct.pack_into(">H", catalog, node + 510, 14)
                catalog[key] = 6 + len(name)
                catalog[key + 6] = len(name)
                catalog[key + 7:key + 7 + len(name)] = name
                body = (key + 1 + catalog[key] + 1) & ~1
                catalog[body:body + 2] = b"\x02\x00"
                struct.pack_into(">I", catalog, body + 30, block_size)
            file.seek(start + allocation_start + block_size)
            file.write(catalog)
            file.seek(start + allocation_start + 5 * block_size + 4096)
            file.write(b"PRIVATE SOURCE FILE MUST NOT BE COPIED")
        return start

    def fresh(self, source, output, capacity):
        layout = prepare.wrapper_geometry(source, capacity)
        with output.open("wb") as file:
            file.write(prepare.partition_prefix(source, capacity, layout["length"]))
            file.truncate(capacity)
            start, _ = prepare.partition(output)
            header = bytearray(512)
            header[:4] = b"H+\0\4"
            struct.pack_into(">II", header, 40, 4096, layout["length"] // 4096)
            struct.pack_into(">I", header, 80, 21)
            struct.pack_into(">I", header, 92, 21)
            file.seek(start + 1024)
            file.write(header)
            file.seek(start + 4096)
            file.write(b"FRESH APPROVED SYSTEM FILE")
        return layout, header

    def testResizedFilesystemAndBothAlternateHeadersUseActualEnds(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = [Path(directory) / name for name in ("source.img", "fresh.img")]
            self.fixture(source)
            before = hashlib.sha256(source.read_bytes()).digest()
            capacity = 32 * 1024 ** 2
            layout, expected_header = self.fresh(source, output, capacity)
            preexisting = output.with_name(".wrapped-disk.img")
            preexisting.write_bytes(b"ANOTHER PREPARATION OWNS THIS FILE")
            prepare.retain_classic_wrapper(source, output)
            self.assertEqual(preexisting.read_bytes(), b"ANOTHER PREPARATION OWNS THIS FILE")
            self.assertEqual(output.stat().st_size, capacity)
            self.assertLess(output.stat().st_blocks * 512, 2 * 1024 ** 2)
            self.assertEqual(prepare.partition(output), (layout["source_start"], layout["partition_size"]))
            self.assertGreater(layout["length"], 31 * 1024 ** 2)
            with output.open("rb") as file:
                for offset in (layout["start"] + 1024, layout["start"] + layout["length"] - 1024):
                    file.seek(offset)
                    self.assertEqual(file.read(512), expected_header)
                file.seek(layout["start"] + 4096)
                marker = b"FRESH APPROVED SYSTEM FILE"
                self.assertEqual(file.read(len(marker)), marker)
                file.seek(layout["source_start"] + 1024)
                wrapper = file.read(512)
                self.assertGreater(struct.unpack_from(">I", wrapper, 20)[0], layout["old_block_size"])
                file.seek(layout["source_start"] + layout["partition_size"] - 1024)
                self.assertEqual(file.read(512), wrapper)
                file.seek(layout["source_start"] + layout["allocation_start"] + layout["block_size"])
                catalog = file.read(layout["old_block_size"])
                key = 512 + 14
                body = (key + 1 + catalog[key] + 1) & ~1
                self.assertEqual(struct.unpack_from(">I", catalog, body + 30)[0], layout["block_size"])
            self.assertNotIn(b"PRIVATE SOURCE FILE", output.read_bytes())
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)

    def testUnexpectedWrapperFilesAndImpossibleCapacityAreRejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            start = self.fixture(source)
            with self.assertRaisesRegex(ValueError, "capacity"):
                prepare.wrapper_geometry(source, 8 * 1024 ** 2)
            with source.open("rb") as file:
                file.seek(start + 24 * 512 + 4096)
                catalog = file.read(4096).replace(b"Finder", b"Secret")
            with self.assertRaisesRegex(ValueError, "Unexpected file"):
                prepare.resize_wrapper_catalog(catalog, 4096, 8192)

    def testPartitionMapDescribesFull32GiBDiskAndNewPayloadExtent(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            self.fixture(source)
            capacity = 32 * 1024 ** 3
            hfs_size = capacity - 64 * 512 - 17 * 512
            prefix = prepare.partition_prefix(source, capacity, hfs_size)
            self.assertEqual(struct.unpack_from(">I", prefix, 4)[0] * 512, capacity)
            self.assertEqual(struct.unpack_from(">I", prefix, 1024 + 12)[0] * 512, hfs_size)
            free_start, free_size = struct.unpack_from(">II", prefix, 1536 + 8)
            self.assertEqual((free_start + free_size) * 512, capacity)
            self.assertEqual(free_size, 17)


if __name__ == "__main__":
    unittest.main()
