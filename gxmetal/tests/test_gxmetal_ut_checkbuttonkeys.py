#!/usr/bin/env python3
"""Focused unit tests for the read-only UT CheckButtonKeys probe."""

import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


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

    def test_locator_uses_invariant_subwindow_and_verifies_runtime_base(self):
        code = b"".join(index.to_bytes(4, "big") for index in range(128))
        runtime_base = 0x10000
        runtime_code = bytearray(code)
        sampled_offset = 200
        runtime_code[sampled_offset:sampled_offset + 4] = b"\xff" * 4

        class Client:
            running = False

            def register(self, number):
                if number == 64:
                    return runtime_base + sampled_offset
                return 0

            def memory(self, address, length):
                offset = address - runtime_base
                if offset < 0 or offset + length > len(runtime_code):
                    raise RuntimeError("unmapped")
                return bytes(runtime_code[offset:offset + length])

            def continue_guest(self):
                self.running = True

            def interrupt(self):
                self.running = False

        located, observations = PROBE.locate_code_fragment(
            Client(), code, samples=1, interval=0,
            verify_offsets=(40, 80, 120))
        self.assertEqual(located, runtime_base)
        self.assertEqual(observations[0]["matched_register"], "pc")
        self.assertGreaterEqual(observations[0]["match_bytes"], 12)

    def test_locator_failure_preserves_observations(self):
        class Client:
            running = False

            def register(self, number):
                return 0x20000 + number * 4

            def memory(self, address, length):
                return b"\xff" * length

            def continue_guest(self):
                self.running = True

            def interrupt(self):
                self.running = False

        with self.assertRaises(PROBE.CodeFragmentNotFound) as raised:
            PROBE.locate_code_fragment(
                Client(), bytes(range(128)), samples=1, interval=0,
                verify_offsets=(4, 8, 12))
        self.assertEqual(len(raised.exception.observations), 1)
        self.assertEqual(raised.exception.observations[0]["index"], 0)

    def test_locator_stops_after_three_identical_non_pef_samples(self):
        class Client:
            running = False

            def __init__(self):
                self.continue_calls = 0
                self.interrupt_calls = 0
                self.breakpoint_calls = 0

            def register(self, number):
                return {64: 0xF2230C, 67: 0xF227EC}[number]

            def memory(self, address, length):
                return b"\xff" * length

            def continue_guest(self):
                self.continue_calls += 1
                self.running = True

            def interrupt(self):
                self.interrupt_calls += 1
                self.running = False

            def breakpoint(self, address, insert):
                self.breakpoint_calls += 1

        client = Client()
        with self.assertRaises(PROBE.LocatorInconclusive) as raised:
            PROBE.locate_code_fragment(
                client, bytes(range(128)), samples=320, interval=0,
                verify_offsets=(4, 8, 12))

        observations = raised.exception.observations
        self.assertEqual(len(observations), 3)
        self.assertEqual(observations[-1]["identical_non_pef_samples"], 3)
        self.assertEqual(client.continue_calls, 2)
        self.assertEqual(client.interrupt_calls, 2)
        self.assertEqual(client.breakpoint_calls, 0)

    def test_main_writes_locator_failure_before_nonzero_exit(self):
        result = {
            "schema": 1,
            "outcome": "locator-failed",
            "error": "no live code match",
            "locator_samples": [{"index": 0, "pc": "0x1234"}],
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "probe.json"
            argv = [
                "gxmetal-ut-checkbuttonkeys.py",
                "monitor.sock",
                "events.jsonl",
                "ut.pef",
                str(output),
                "--key-step-index",
                "29",
            ]
            with (mock.patch.object(sys, "argv", argv),
                  mock.patch.object(
                      PROBE, "run",
                      side_effect=PROBE.ProbeFailure(result)),
                  mock.patch("sys.stdout", new_callable=io.StringIO)):
                with self.assertRaises(SystemExit) as raised:
                    PROBE.main()
            self.assertEqual(raised.exception.code, 1)
            self.assertEqual(json.loads(output.read_text()), result)


if __name__ == "__main__":
    unittest.main()
