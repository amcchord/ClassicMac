#!/usr/bin/env python3
"""Protocol-level tests for the dependency-free GXMetal VNC helper."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "gxmetal_vnc", ROOT / "scripts" / "gxmetal-vnc.py")
VNC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VNC)
SWEEP_SPEC = importlib.util.spec_from_file_location(
    "gxmetal_game_sweep", ROOT / "scripts" / "gxmetal-game-sweep.py")
SWEEP = importlib.util.module_from_spec(SWEEP_SPEC)
sys.modules[SWEEP_SPEC.name] = SWEEP
SWEEP_SPEC.loader.exec_module(SWEEP)


class FakeSocket:
    def __init__(self, incoming=b""):
        self.incoming = bytearray(incoming)
        self.sent = bytearray()

    def recv(self, length):
        result = self.incoming[:length]
        del self.incoming[:length]
        return bytes(result)

    def sendall(self, data):
        self.sent.extend(data)


def framebuffer_update(x, y, width, height, encoding, payload=b""):
    return (struct.pack(">BBH", 0, 0, 1) +
            struct.pack(">HHHHi", x, y, width, height, encoding) + payload)


class RFBClientTests(unittest.TestCase):
    def test_connect_requests_raw_and_desktop_resize_encodings(self):
        incoming = (
            b"RFB 003.008\n" + b"\x01\x01" + struct.pack(">I", 0) +
            struct.pack(">HH", 1024, 768) + bytes(16) +
            struct.pack(">I", 0))
        connection = FakeSocket(incoming)
        client = VNC.RFBClient(connection)

        client.connect()

        self.assertEqual((client.width, client.height), (1024, 768))
        self.assertTrue(connection.sent.endswith(
            struct.pack(">BBHiii", 2, 0, 3, 0, -223, -308)))

    def test_capture_restarts_after_desktop_resize(self):
        resized = framebuffer_update(0, 0, 2, 1, -223)
        pixels = framebuffer_update(
            0, 0, 2, 1, 0,
            bytes((3, 2, 1, 0, 30, 20, 10, 0)))
        connection = FakeSocket(resized + pixels)
        client = VNC.RFBClient(connection)
        client.width = 4
        client.height = 3
        client.pointer_x = 1
        client.pointer_y = 1

        rgb = client.capture()

        self.assertEqual((client.width, client.height), (2, 1))
        self.assertIsNone(client.pointer_x)
        self.assertIsNone(client.pointer_y)
        self.assertEqual(rgb, bytes((1, 2, 3, 10, 20, 30)))
        requests = bytes(connection.sent)
        self.assertEqual(requests, (
            struct.pack(">BBHHHH", 3, 0, 0, 0, 4, 3) +
            struct.pack(">BBHHHH", 3, 0, 0, 0, 2, 1)))

    def test_capture_consumes_extended_desktop_layout(self):
        screen = struct.pack(">IHHHHI", 7, 0, 0, 1, 1, 0)
        resized = framebuffer_update(
            0, 0, 1, 1, -308, bytes((1, 0, 0, 0)) + screen)
        pixel = framebuffer_update(0, 0, 1, 1, 0,
                                   bytes((0x33, 0x22, 0x11, 0)))
        connection = FakeSocket(resized + pixel)
        client = VNC.RFBClient(connection)
        client.width = 2
        client.height = 2

        self.assertEqual(client.capture(), bytes((0x11, 0x22, 0x33)))
        self.assertEqual((client.width, client.height), (1, 1))


class ManifestValidationTests(unittest.TestCase):
    def test_qemu_guest_name_removes_option_delimiters(self):
        self.assertEqual(
            SWEEP.qemu_guest_name(
                "Combat Mission, Oni, and Unreal Tournament", "gxmetal"),
            "GXMetal sweep: Combat Mission Oni and Unreal Tournament "
            "(gxmetal)")

    def test_named_keys_are_case_sensitive_and_checked_before_runtime(self):
        self.assertEqual(
            SWEEP.validate_step({"key": "Space"}, "steps[0]"),
            {"key": "Space"})
        with self.assertRaisesRegex(ValueError, "unknown VNC key: space"):
            SWEEP.validate_step({"key": "space"}, "steps[0]")

    def test_chord_keys_are_validated(self):
        self.assertEqual(
            SWEEP.validate_step({"chord": "Super_L+o"}, "steps[0]"),
            {"chord": "Super_L+o"})
        with self.assertRaisesRegex(ValueError, "unknown or missing"):
            SWEEP.validate_step({"chord": "Command+o"}, "steps[0]")

    def test_frame_change_wait_is_validated(self):
        step = {
            "wait_for_frame_change": {
                "timeout_seconds": 30,
                "poll_interval_seconds": 0.5,
                "minimum_changed_fraction": 0.1,
                "channel_tolerance": 4,
            },
            "capture_after": True,
        }
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "must not exceed 1"):
            SWEEP.validate_step({
                "wait_for_frame_change": {
                    "timeout_seconds": 30,
                    "minimum_changed_fraction": 1.1,
                }
            }, "steps[0]")

    def test_changed_pixel_fraction_uses_channel_tolerance(self):
        previous = bytes((10, 20, 30, 40, 50, 60))
        current = bytes((11, 21, 31, 40, 50, 70))
        self.assertEqual(
            SWEEP.changed_pixel_fraction(previous, current, 3), 0.5)
        self.assertEqual(
            SWEEP.changed_pixel_fraction(previous, current, 10), 0.0)


if __name__ == "__main__":
    unittest.main()
