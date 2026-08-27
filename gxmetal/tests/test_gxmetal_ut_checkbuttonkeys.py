#!/usr/bin/env python3
"""Focused unit tests for the read-only UT CheckButtonKeys probe."""

import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "gxmetal_ut_checkbuttonkeys",
    ROOT / "scripts" / "gxmetal-ut-checkbuttonkeys.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class ProbeTests(unittest.TestCase):
    def test_rsp_packet_checksum(self):
        self.assertEqual(PROBE.RSPClient.framed("p40"), b"$p40#d4")

    def test_pef_code_accepts_only_bounded_powerpc_code_section(self):
        data = bytearray(128)
        data[:12] = b"Joy!peffpwpc"
        struct.pack_into(">H", data, 32, 1)
        struct.pack_into(
            ">iIIIII4B", data, 40,
            -1, 0, 16, 16, 16, 96, 0, 4, 4, 0)
        data[96:112] = bytes(range(16))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ut.pef"
            path.write_bytes(data)
            self.assertEqual(PROBE.pef_code(path), (bytes(range(16)), 96))

            struct.pack_into(">I", data, 40 + 20, 120)
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "exceeds"):
                PROBE.pef_code(path)

    def test_key_down_marker_requires_exact_step_and_control(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "events.jsonl"
            path.write_text("\n".join((
                json.dumps({
                    "event": "step", "index": 7,
                    "action": "key_down", "value": "Shift_L",
                }),
                json.dumps({
                    "event": "step", "index": 8,
                    "action": "key_down", "value": "Control_L",
                }),
            )), encoding="utf-8")
            self.assertFalse(PROBE.key_down_seen(path, 7))
            self.assertTrue(PROBE.key_down_seen(path, 8))
            self.assertTrue(PROBE.step_seen(path, 7, "key_down"))
            self.assertFalse(PROBE.step_seen(path, 7, "screenshot"))


if __name__ == "__main__":
    unittest.main()
