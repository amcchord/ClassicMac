#!/usr/bin/env python3
"""Test content-derived sparse bounds independently of host file allocation."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("packager", Path(__file__).with_name("package-machine-template.py"))
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class SparseAccountingTests(unittest.TestCase):
    def test_irregular_reads_preserve_content_and_boundary_accounting(self):
        chunk = packager.SPARSE_CHUNK_BYTES
        content = bytearray(6 * chunk + 512)
        for offset in (0, chunk - 1, 2 * chunk, len(content) - 1):
            content[offset] = 71
        for read_size in (16_384, 70_001, chunk, 2 * chunk + 17):
            with self.subTest(read_size=read_size):
                reader = packager.SparseAccountingReader(io.BytesIO(content))
                result = b"".join(iter(lambda: reader.read(read_size), b""))
                self.assertEqual(result, content)
                self.assertEqual(reader.required_storage, 3 * chunk)
                self.assertEqual(reader.bytes_read, len(content))

    def test_physically_written_zero_disk_still_costs_zero_disk_chunks(self):
        chunk = packager.SPARSE_CHUNK_BYTES
        with tempfile.TemporaryDirectory() as folder:
            disk = Path(folder) / "disk.img"
            # Write the complete payload rather than creating a seek/truncate
            # hole: filesystem allocation must not determine catalog guidance.
            disk.write_bytes(b"\0" * (4 * chunk))
            with disk.open("rb") as source:
                reader = packager.SparseAccountingReader(source)
                while reader.read(16_384):
                    pass
            self.assertEqual(reader.required_storage, 0)
            self.assertEqual(reader.bytes_read, disk.stat().st_size)

    def test_partial_nonzero_chunk_and_other_files_round_up(self):
        reader = packager.SparseAccountingReader(io.BytesIO(b"\0" * 511 + b"x"))
        self.assertEqual(reader.read(512), b"\0" * 511 + b"x")
        self.assertEqual(reader.required_storage, packager.SPARSE_CHUNK_BYTES)
        self.assertEqual(packager.storage_charge(512), packager.SPARSE_CHUNK_BYTES)
        self.assertEqual(packager.storage_charge(packager.SPARSE_CHUNK_BYTES + 1), 2 * packager.SPARSE_CHUNK_BYTES)


if __name__ == "__main__":
    unittest.main()
