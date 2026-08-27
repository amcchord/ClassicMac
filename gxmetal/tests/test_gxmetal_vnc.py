#!/usr/bin/env python3
"""Protocol-level tests for the dependency-free GXMetal VNC helper."""

import argparse
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


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
    def test_function_keysyms_cover_classic_game_controls(self):
        client = VNC.RFBClient(FakeSocket())
        self.assertEqual(client.keysym("F1"), 0xFFBE)
        self.assertEqual(client.keysym("F12"), 0xFFC9)
        self.assertEqual(set(VNC.KEYSYMS), set(SWEEP.VALID_NAMED_KEYS))

    def test_hold_click_sends_down_then_up_after_requested_interval(self):
        connection = FakeSocket()
        client = VNC.RFBClient(connection)
        client.width = 640
        client.height = 480
        client.pointer_x = 320
        client.pointer_y = 240

        with mock.patch.object(VNC.time, "sleep") as sleep:
            client.hold_click(320, 240, 0.75)

        self.assertEqual(
            bytes(connection.sent),
            struct.pack(">BBHH", 5, 1, 320, 240) +
            struct.pack(">BBHH", 5, 0, 320, 240))
        sleep.assert_called_once_with(0.75)

    def test_hold_click_releases_button_when_wait_is_interrupted(self):
        connection = FakeSocket()
        client = VNC.RFBClient(connection)
        client.width = 640
        client.height = 480
        client.pointer_x = 320
        client.pointer_y = 240

        with mock.patch.object(
                VNC.time, "sleep", side_effect=RuntimeError("interrupted")):
            with self.assertRaisesRegex(RuntimeError, "interrupted"):
                client.hold_click(320, 240, 0.75)

        self.assertEqual(
            bytes(connection.sent),
            struct.pack(">BBHH", 5, 1, 320, 240) +
            struct.pack(">BBHH", 5, 0, 320, 240))

    def test_parse_pixel_accepts_optional_tolerance_and_rejects_channels(self):
        self.assertEqual(
            VNC.parse_pixel("128,84,248,248,0"),
            (128, 84, 248, 248, 0, 8))
        self.assertEqual(
            VNC.parse_pixel("128,84,248,248,0,12"),
            (128, 84, 248, 248, 0, 12))
        with self.assertRaisesRegex(argparse.ArgumentTypeError,
                                    "0 through 255"):
            VNC.parse_pixel("128,84,256,248,0")

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
    def test_game_id_filter_is_validated_and_preserves_manifest_order(self):
        specs = [
            SWEEP.RunSpec(
                game_id=game_id, name=game_id, mode="gxmetal", cdrom=None,
                source_url=None, source_sha256=None, boot_wait_seconds=0,
                observation_seconds=0, capture_interval_seconds=1,
                resolution="640x480x15", steps=())
            for game_id in ("bugdom", "future-cop", "weekend-warrior")
        ]
        self.assertEqual(
            [spec.game_id for spec in SWEEP.select_game_specs(
                specs, SWEEP.parse_game_ids("weekend-warrior,bugdom"))],
            ["bugdom", "weekend-warrior"])
        with self.assertRaisesRegex(ValueError, "missing-game"):
            SWEEP.select_game_specs(specs, ("missing-game",))
        with self.assertRaises(argparse.ArgumentTypeError):
            SWEEP.parse_game_ids("Bugdom")

    def test_qemu_guest_name_removes_option_delimiters(self):
        self.assertEqual(
            SWEEP.qemu_guest_name(
                "Combat Mission, Oni, and Unreal Tournament", "gxmetal"),
            "GXMetal sweep: Combat Mission Oni and Unreal Tournament "
            "(gxmetal)")

    def test_sweep_audio_backend_defaults_to_none(self):
        self.assertEqual(SWEEP.DEFAULT_AUDIO_BACKEND, "none")
        self.assertEqual(
            SWEEP.qemu_audio_device_spec(SWEEP.DEFAULT_AUDIO_BACKEND),
            "none,id=snd0")

    def test_sweep_coreaudio_matches_power_mac_launcher(self):
        self.assertEqual(
            SWEEP.qemu_audio_device_spec("coreaudio"),
            "coreaudio,id=snd0,out.buffer-length=50000")
        with self.assertRaisesRegex(ValueError, "unsupported audio backend"):
            SWEEP.qemu_audio_device_spec("invalid")

    def test_qemu_command_selects_requested_audio_backend(self):
        spec = SWEEP.RunSpec(
            game_id="audio-probe", name="Audio Probe", mode="gxmetal",
            cdrom=None, source_url=None, source_sha256=None,
            boot_wait_seconds=0, observation_seconds=0,
            capture_interval_seconds=1, resolution="640x480x15", steps=())
        command = SWEEP.qemu_command(
            ROOT, Path("/qemu"), Path("/disk.img"), Path("/evidence"),
            Path("/sockets"), spec, Path("/ndrvloader"), Path("/pc-bios"),
            None, 512, "7400", "tcg,tb-size=512", "coreaudio")

        audio_option = command.index("-audiodev")
        self.assertEqual(
            command[audio_option + 1],
            "coreaudio,id=snd0,out.buffer-length=50000")
        self.assertEqual(command.count("-audiodev"), 1)
        log_option = command.index("-d")
        self.assertEqual(command[log_option + 1], "guest_errors")

    def test_named_keys_are_case_sensitive_and_checked_before_runtime(self):
        self.assertEqual(
            SWEEP.validate_step({"key": "Space"}, "steps[0]"),
            {"key": "Space"})
        self.assertEqual(
            SWEEP.validate_step({"key": "F1"}, "steps[0]"),
            {"key": "F1"})
        with self.assertRaisesRegex(ValueError, "unknown VNC key: space"):
            SWEEP.validate_step({"key": "space"}, "steps[0]")

    def test_chord_keys_are_validated(self):
        self.assertEqual(
            SWEEP.validate_step({"chord": "Super_L+o"}, "steps[0]"),
            {"chord": "Super_L+o"})
        with self.assertRaisesRegex(ValueError, "unknown or missing"):
            SWEEP.validate_step({"chord": "Command+o"}, "steps[0]")

    def test_drag_coordinates_are_validated(self):
        step = {"drag": [250, 68, 250, 20]}
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "start_x"):
            SWEEP.validate_step({"drag": [250, -1, 250, 20]}, "steps[0]")

    def test_hold_click_coordinates_and_duration_are_validated(self):
        step = {"hold_click": [595, 418], "hold_ms": 750}
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "nonnegative"):
            SWEEP.validate_step(
                {"hold_click": [-1, 418], "hold_ms": 750}, "steps[0]")
        with self.assertRaisesRegex(ValueError, "positive"):
            SWEEP.validate_step(
                {"hold_click": [595, 418], "hold_ms": 0}, "steps[0]")

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

    def test_named_frame_change_assertion_is_validated(self):
        step = {
            "assert_frame_changed_since": {
                "screenshot": "before-input",
                "minimum_changed_fraction": 0.1,
                "channel_tolerance": 4,
                "region": [100, 120, 320, 180],
            }
        }
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "must not exceed 1"):
            SWEEP.validate_step({
                "assert_frame_changed_since": {
                    "screenshot": "before-input",
                    "minimum_changed_fraction": 1.1,
                }
            }, "steps[0]")
        with self.assertRaisesRegex(ValueError, "region must be"):
            SWEEP.validate_step({
                "assert_frame_changed_since": {
                    "screenshot": "before-input",
                    "region": [10, 20, 0, 30],
                }
            }, "steps[0]")

    def test_named_frame_change_must_reference_earlier_screenshot(self):
        manifest = {
            "schema": 1,
            "games": [{
                "id": "input-gate",
                "name": "Input gate",
                "modes": ["gxmetal"],
                "steps": [
                    {"assert_frame_changed_since": {
                        "screenshot": "before-input",
                    }},
                    {"screenshot": "before-input"},
                ],
            }],
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(__import__("json").dumps(manifest),
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown or later"):
                SWEEP.load_manifest(path, ("gxmetal",))

    def test_pixel_wait_is_validated(self):
        step = {
            "wait_for_pixel": {
                "x": 324,
                "y": 140,
                "red": 248,
                "green": 248,
                "blue": 0,
                "tolerance": 8,
                "timeout_seconds": 240,
                "poll_interval_seconds": 1,
            },
            "capture_after": True,
        }
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        invalid = dict(step["wait_for_pixel"], red=256)
        with self.assertRaisesRegex(ValueError, "red must be an integer"):
            SWEEP.validate_step(
                {"wait_for_pixel": invalid}, "steps[0]")

    def test_changed_pixel_fraction_uses_channel_tolerance(self):
        previous = bytes((10, 20, 30, 40, 50, 60))
        current = bytes((11, 21, 31, 40, 50, 70))
        self.assertEqual(
            SWEEP.changed_pixel_fraction(previous, current, 3), 0.5)
        self.assertEqual(
            SWEEP.changed_pixel_fraction(previous, current, 10), 0.0)

    def test_dominant_color_assertion_is_validated(self):
        step = {
            "assert_dominant_color_fraction_below": {
                "maximum": 0.1,
                "ignore_colors": [[0, 0, 0]],
            }
        }
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "must not exceed 1"):
            SWEEP.validate_step({
                "assert_dominant_color_fraction_below": {"maximum": 1.1}
            }, "steps[0]")
        with self.assertRaisesRegex(ValueError, "RGB integer triplets"):
            SWEEP.validate_step({
                "assert_dominant_color_fraction_below": {
                    "maximum": 0.1,
                    "ignore_colors": [[0, 0, 256]],
                }
            }, "steps[0]")

    def test_dominant_exact_color_fraction_can_ignore_colors(self):
        rgb = bytes((1, 2, 3, 1, 2, 3, 4, 5, 6, 0, 0, 0))
        self.assertEqual(
            SWEEP.dominant_exact_color_fraction(rgb),
            ((1, 2, 3), 0.5))
        self.assertEqual(
            SWEEP.dominant_exact_color_fraction(
                rgb, ((1, 2, 3),)),
            ((4, 5, 6), 0.25))
        self.assertEqual(
            SWEEP.dominant_exact_color_fraction(
                bytes((0, 0, 0)), ((0, 0, 0),)),
            (None, 0.0))

    def test_color_range_assertion_is_validated(self):
        step = {
            "assert_color_range_fraction_below": {
                "minimum_rgb": [224, 224, 208],
                "maximum_rgb": [255, 255, 255],
                "maximum_fraction": 0.1,
                "region": [100, 120, 320, 180],
            }
        }
        self.assertEqual(SWEEP.validate_step(step, "steps[0]"), step)
        with self.assertRaisesRegex(ValueError, "must not exceed 1"):
            SWEEP.validate_step({
                "assert_color_range_fraction_below": {
                    "minimum_rgb": [224, 224, 208],
                    "maximum_rgb": [255, 255, 255],
                    "maximum_fraction": 1.1,
                }
            }, "steps[0]")
        with self.assertRaisesRegex(ValueError, "minimum_rgb must not"):
            SWEEP.validate_step({
                "assert_color_range_fraction_below": {
                    "minimum_rgb": [255, 224, 208],
                    "maximum_rgb": [224, 255, 255],
                    "maximum_fraction": 0.1,
                }
            }, "steps[0]")
        with self.assertRaisesRegex(ValueError, "region must be"):
            SWEEP.validate_step({
                "assert_color_range_fraction_below": {
                    "minimum_rgb": [0, 0, 0],
                    "maximum_rgb": [8, 8, 8],
                    "maximum_fraction": 0.1,
                    "region": [10, 20, 0, 30],
                }
            }, "steps[0]")

    def test_color_range_fraction_is_inclusive(self):
        rgb = bytes((
            223, 224, 208,
            224, 224, 208,
            240, 240, 224,
            255, 255, 255,
            255, 255, 254,
        ))
        self.assertEqual(
            SWEEP.color_range_fraction(
                rgb, (224, 224, 208), (255, 255, 254)),
            0.6)

    def test_crop_rgb_region_preserves_rows(self):
        rgb = bytes(range(4 * 3 * 3))
        self.assertEqual(
            SWEEP.crop_rgb_region(rgb, 4, 3, (1, 1, 2, 2)),
            rgb[15:21] + rgb[27:33])
        with self.assertRaisesRegex(ValueError, "outside 4x3 frame"):
            SWEEP.crop_rgb_region(rgb, 4, 3, (3, 2, 2, 1))

    def test_frame_changed_fraction_supports_crop_and_resize(self):
        baseline = bytes((
            0, 0, 0, 10, 10, 10,
            20, 20, 20, 30, 30, 30,
        ))
        current = bytes((
            100, 100, 100, 10, 10, 10,
            20, 20, 20, 40, 40, 40,
        ))
        self.assertEqual(
            SWEEP.frame_changed_fraction(
                baseline, 2, 2, current, 2, 2, 8),
            0.5)
        self.assertEqual(
            SWEEP.frame_changed_fraction(
                baseline, 2, 2, current, 2, 2, 8, (0, 1, 2, 1)),
            0.5)
        self.assertEqual(
            SWEEP.frame_changed_fraction(
                baseline, 2, 2, b"", 3, 2, 8),
            1.0)

    def test_rgb_pixel_match_uses_tolerance_and_checks_bounds(self):
        rgb = bytes((10, 20, 30, 40, 50, 60))
        self.assertTrue(SWEEP.rgb_pixel_matches(
            rgb, 2, 1, 1, 0, (42, 48, 60), 2))
        self.assertFalse(SWEEP.rgb_pixel_matches(
            rgb, 2, 1, 0, 0, (10, 20, 34), 3))
        with self.assertRaisesRegex(ValueError, "outside 2x1 frame"):
            SWEEP.rgb_pixel_matches(rgb, 2, 1, 2, 0, (0, 0, 0), 0)


if __name__ == "__main__":
    unittest.main()
