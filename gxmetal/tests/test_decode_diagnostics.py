#!/usr/bin/env python3
"""Tests for the schema-derived GXMetal Driver Trace decoder."""

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "decode_gxmetal_diagnostics",
    ROOT / "scripts" / "decode-gxmetal-diagnostics.py")
DECODER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = DECODER
SPEC.loader.exec_module(DECODER)


HEADER = """
#define GXMETAL_DIAGNOSTIC_COUNTER_COUNT 2u
typedef struct GXMetalDiagnosticSnapshot {
    uint32_t version;
    int32_t result;
    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER_COUNT];
} GXMetalDiagnosticSnapshot;
"""


class DiagnosticDecoderTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.header = Path(self.directory.name) / "GXMetalDiagnostics.h"
        self.header.write_text(HEADER, encoding="utf-8")

    def tearDown(self):
        self.directory.cleanup()

    def test_schema_and_big_endian_values_are_decoded(self):
        fields = DECODER.parse_schema(self.header)
        self.assertEqual(fields, [
            ("version", False, 1),
            ("result", True, 1),
            ("counters", False, 2),
        ])
        snapshot = struct.pack(">IiII", 3, -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "version": 3,
            "result": -49,
            "counters": [7, 11],
        })

    def test_snapshot_size_must_match_derived_schema(self):
        fields = DECODER.parse_schema(self.header)
        with self.assertRaisesRegex(ValueError, "header describes 16 bytes"):
            DECODER.decode_snapshot(bytes(12), fields)

    def test_unsupported_header_field_is_rejected(self):
        self.header.write_text(
            HEADER.replace("uint32_t version", "uint64_t version"),
            encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unsupported diagnostic field"):
            DECODER.parse_schema(self.header)


if __name__ == "__main__":
    unittest.main()
