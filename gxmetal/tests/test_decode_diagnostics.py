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
#define GXMETAL_DIAGNOSTIC_COUNTER47_COUNT 2u
typedef struct GXMetalDiagnosticSnapshot {
    uint32_t magic;
    uint32_t version;
    int32_t result;
    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];
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
            ("magic", False, 1),
            ("version", False, 1),
            ("result", True, 1),
            ("counters", False, 2),
        ])
        snapshot = struct.pack(">IIiII", 0x47584447, 3, -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 3,
            "result": -49,
            "counters": [7, 11],
        })

    def test_snapshot_size_must_match_derived_schema(self):
        fields = DECODER.parse_schema(self.header)
        with self.assertRaisesRegex(ValueError, "header describes 20 bytes"):
            DECODER.decode_snapshot(bytes(12), fields)

    def test_unsupported_header_field_is_rejected(self):
        self.header.write_text(
            HEADER.replace("uint32_t version", "uint64_t version"),
            encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "unsupported diagnostic field"):
            DECODER.parse_schema(self.header)

    def test_version_1000c_accepts_all_missing_appended_fields(self):
        trailing_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.CONTEXT_LIFECYCLE_FIELDS
                         + DECODER.FOG_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_TRACE_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                         + DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + trailing_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x0001000C,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x0001000C,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_1000d_accepts_missing_appended_fog_fields(self):
        fog_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.FOG_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_TRACE_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                         + DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + fog_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x0001000D,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x0001000D,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_1000e_accepts_missing_private_trace_fields(self):
        private_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_TRACE_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                         + DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + private_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x0001000E,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x0001000E,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_1000f_accepts_missing_draw47_fields(self):
        draw_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_DRAW47_FIELDS
                         + DECODER.ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                         + DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + draw_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x0001000F,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x0001000F,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10010_accepts_missing_draw47_vertex_fields(self):
        vertex_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                         + DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + vertex_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010010,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010010,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10011_accepts_missing_clear_state_fields(self):
        clear_state_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_CLEAR_STATE_FIELDS
                         + DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                         + DECODER.ATI_PRIVATE_STRIP_FIELDS
                         + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                         + DECODER.ATI_PRIVATE_FILL_FIELDS
                         + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + clear_state_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010011,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010011,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10012_accepts_missing_pixel21_fields(self):
        pixel21_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_PIXEL21_FIELDS
                          + DECODER.ATI_PRIVATE_STRIP_FIELDS
                          + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                          + DECODER.ATI_PRIVATE_FILL_FIELDS
                          + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                          + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + pixel21_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010012,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010012,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10013_accepts_missing_strip_fields(self):
        strip_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_STRIP_FIELDS
                          + DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                          + DECODER.ATI_PRIVATE_FILL_FIELDS
                          + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                          + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + strip_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010013,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010013,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10014_accepts_missing_primitive_fields(self):
        primitive_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_PRIMITIVE_FIELDS
                          + DECODER.ATI_PRIVATE_FILL_FIELDS
                          + DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                          + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + primitive_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010014,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010014,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10015_accepts_missing_geometry_argument_fields(self):
        geometry_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_GEOMETRY_FIELDS
                         + DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + geometry_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010015,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010015,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10016_accepts_missing_vertex_capture_fields(self):
        capture_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in DECODER.ATI_PRIVATE_VERTEX_CAPTURE_FIELDS)
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + capture_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010016,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010016,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10017_accepts_missing_draw48_capture_fields(self):
        capture_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_DRAW48_CAPTURE_FIELDS
                         + DECODER.ATI_PRIVATE_FINISH_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + capture_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010017,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010017,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10018_accepts_missing_finish_diagnostics(self):
        finish_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in DECODER.ATI_PRIVATE_FINISH_FIELDS)
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + finish_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010018,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010018,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_10019_accepts_missing_finish_detail_fields(self):
        detail_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in (DECODER.ATI_PRIVATE_FINISH_DETAIL_FIELDS
                         + DECODER.ATI_PRIVATE_TRANSITION_FIELDS))
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + detail_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x00010019,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x00010019,
            "result": -49,
            "counters": [7, 11],
        })

    def test_version_1001a_accepts_missing_transition_fields(self):
        transition_declarations = "\n".join(
            f"    uint32_t {name};"
            for name in DECODER.ATI_PRIVATE_TRANSITION_FIELDS)
        self.header.write_text(
            HEADER.replace(
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];",
                "    uint32_t counters[GXMETAL_DIAGNOSTIC_COUNTER47_COUNT];\n"
                + transition_declarations),
            encoding="utf-8")
        fields = DECODER.parse_schema(self.header)
        snapshot = struct.pack(">IIiII", 0x47584447, 0x0001001A,
                               -49, 7, 11)
        self.assertEqual(DECODER.decode_snapshot(snapshot, fields), {
            "magic": 0x47584447,
            "version": 0x0001001A,
            "result": -49,
            "counters": [7, 11],
        })


if __name__ == "__main__":
    unittest.main()
