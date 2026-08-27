#!/usr/bin/env python3
"""Fail-closed tests for disposable Reality Bytes trace preparation."""

import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_reality_bytes_runtime_trace",
    ROOT / "scripts" / "prepare-reality-bytes-runtime-trace.py")
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


class PreparationSafetyTests(unittest.TestCase):
    def test_refuses_host_writable_source_before_clone(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "output.img"
            source.write_bytes(b"source")
            source.chmod(0o644)

            with mock.patch.object(
                    sys, "argv", ["prepare", "havoc", str(source),
                                  str(output)]), \
                    mock.patch.object(sys, "stderr", new=io.StringIO()), \
                    mock.patch.object(TRACE, "clone_disk") as clone:
                with self.assertRaises(SystemExit) as raised:
                    TRACE.main()

            self.assertEqual(raised.exception.code, 2)
            clone.assert_not_called()
            self.assertFalse(output.exists())

    def test_attach_failure_relocks_and_removes_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "output.img"
            source.write_bytes(b"immutable source")
            source.chmod(0o444)
            source_hash = TRACE.sha256(source)

            def clone_disk(source_path, destination_path):
                shutil.copyfile(source_path, destination_path)
                return "test-copy"

            attach_error = subprocess.CalledProcessError(
                1, ["hdiutil", "attach"])
            with mock.patch.object(
                    sys, "argv", ["prepare", "havoc", str(source),
                                  str(output)]), \
                    mock.patch.object(TRACE, "clone_disk",
                                      side_effect=clone_disk), \
                    mock.patch.object(TRACE.subprocess, "run",
                                      side_effect=attach_error):
                with self.assertRaises(subprocess.CalledProcessError):
                    TRACE.main()

            self.assertEqual(TRACE.sha256(source), source_hash)
            self.assertEqual(source.stat().st_mode & 0o222, 0)
            self.assertFalse(output.exists())

    def test_partial_clone_failure_removes_output_and_rechecks_source(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "output.img"
            source.write_bytes(b"immutable source")
            source.chmod(0o444)
            source_hash = TRACE.sha256(source)

            def fail_clone(_source_path, destination_path):
                destination_path.write_bytes(b"partial writable clone")
                destination_path.chmod(0o644)
                raise OSError("copy interrupted")

            with mock.patch.object(
                    sys, "argv", ["prepare", "havoc", str(source),
                                  str(output)]), \
                    mock.patch.object(TRACE, "clone_disk",
                                      side_effect=fail_clone):
                with self.assertRaisesRegex(OSError, "copy interrupted"):
                    TRACE.main()

            self.assertEqual(TRACE.sha256(source), source_hash)
            self.assertEqual(source.stat().st_mode & 0o222, 0)
            self.assertFalse(output.exists())

    def test_detach_retries_with_force_before_publishing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.img"
            output = Path(directory) / "output.img"
            source.write_bytes(b"immutable source")
            source.chmod(0o444)
            source_hash = TRACE.sha256(source)
            detach_commands = []

            def clone_disk(source_path, destination_path):
                shutil.copyfile(source_path, destination_path)
                return "test-copy"

            def run(command, **_kwargs):
                if command[1] == "attach":
                    mountpoint = Path(command[command.index("-mountpoint") + 1])
                    application = mountpoint / TRACE.GAMES["havoc"]["application"]
                    application.parent.mkdir(parents=True)
                    application.write_bytes(b"test application")
                    return subprocess.CompletedProcess(command, 0, "attached")
                detach_commands.append(command)
                return subprocess.CompletedProcess(
                    command, 0 if "-force" in command else 1,
                    "detached" if "-force" in command else "busy")

            expected_application_hash = \
                TRACE.GAMES["havoc"]["application_sha256"]

            def sha256(path):
                if path == source:
                    return source_hash
                if path.name == "HAVOC\N{TRADE MARK SIGN}":
                    return expected_application_hash
                return "a" * 64

            with mock.patch.object(
                    sys, "argv", ["prepare", "havoc", str(source),
                                  str(output)]), \
                    mock.patch.object(TRACE, "clone_disk",
                                      side_effect=clone_disk), \
                    mock.patch.object(TRACE.subprocess, "run",
                                      side_effect=run), \
                    mock.patch.object(TRACE, "patch_application",
                                      return_value=[]), \
                    mock.patch.object(TRACE, "sha256", side_effect=sha256), \
                    mock.patch.object(sys, "stdout", new=io.StringIO()):
                TRACE.main()

            self.assertEqual(len(detach_commands), 2)
            self.assertNotIn("-force", detach_commands[0])
            self.assertIn("-force", detach_commands[1])
            self.assertTrue(output.exists())
            self.assertEqual(output.stat().st_mode & 0o222, 0)
            self.assertTrue(output.with_suffix(".img.trace.json").is_file())


if __name__ == "__main__":
    unittest.main()
