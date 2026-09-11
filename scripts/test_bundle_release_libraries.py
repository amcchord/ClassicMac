#!/usr/bin/env python3
"""Failure-boundary tests for release library staging; no network or Homebrew writes."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("release_libraries", Path(__file__).with_name("bundle-release-libraries.py"))
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)


class ReleaseLibraryTests(unittest.TestCase):
    def archive(self, target):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w") as archive:
            regular = tarfile.TarInfo("demo/1.0/lib/libdemo.1.dylib")
            regular.size = 5
            archive.addfile(regular, io.BytesIO(b"dylib"))
            alias = tarfile.TarInfo("demo/1.0/lib/libdemo.dylib")
            alias.type = tarfile.SYMTYPE
            alias.linkname = target
            archive.addfile(alias)
        stream.seek(0)
        return tarfile.open(fileobj=stream, mode="r")

    def test_resolves_alias_to_only_regular_library(self):
        with self.archive("libdemo.1.dylib") as archive:
            member = lib.regular_member(archive, "demo/1.0/lib/libdemo.dylib", "demo/1.0")
            self.assertEqual(archive.extractfile(member).read(), b"dylib")

    def test_rejects_escape_absolute_and_cyclic_aliases(self):
        for target in ("../../../outside", "/tmp/outside", "libdemo.dylib"):
            with self.subTest(target=target), self.archive(target) as archive:
                with self.assertRaises(ValueError):
                    lib.regular_member(archive, "demo/1.0/lib/libdemo.dylib", "demo/1.0")

    def test_corrupt_cache_fails_without_network_or_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            checksum = hashlib.sha256(b"expected").hexdigest()
            path = Path(folder) / (checksum + ".tar.gz")
            path.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "Cached bottle SHA256 mismatch"):
                lib.cached_bottle(None, {"sha256": checksum}, Path(folder))
            self.assertEqual(path.read_bytes(), b"corrupt")

    def test_bad_download_is_never_cached(self):
        class Registry:
            def open(self, url):
                return io.BytesIO(b"corrupt")
        with tempfile.TemporaryDirectory() as folder:
            checksum = hashlib.sha256(b"expected").hexdigest()
            with self.assertRaisesRegex(ValueError, "Downloaded bottle SHA256 mismatch"):
                lib.cached_bottle(Registry(), {"sha256": checksum, "url": "unused"}, Path(folder))
            self.assertEqual(list(Path(folder).iterdir()), [])

    def test_receipt_version_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(lib, "PREFIX", Path(folder)):
            prefix = Path(folder)
            keg = prefix / "Cellar/demo/1.0"
            (keg / "lib").mkdir(parents=True)
            (keg / "lib/libdemo.dylib").write_bytes(b"dylib")
            (prefix / "lib").mkdir()
            (prefix / "lib/libdemo.dylib").symlink_to(keg / "lib/libdemo.dylib")
            (keg / "INSTALL_RECEIPT.json").write_text(json.dumps({"arch": "arm64", "source": {"tap": "homebrew/core", "spec": "stable", "versions": {"stable": "2.0"}}}))
            with self.assertRaisesRegex(ValueError, "Receipt/version mismatch"):
                lib.origin("libdemo.dylib")

    def test_historical_registry_cannot_upgrade_version(self):
        class Registry:
            def document(self, url, expected=None):
                if "formulae.brew.sh" in url:
                    return {"full_name": "demo", "tap": "homebrew/core", "versions": {"stable": "2.0"}, "revision": 0}, "0" * 64
                return {"annotations": {"com.github.package.type": "homebrew_bottle", "org.opencontainers.image.title": "demo", "org.opencontainers.image.version": "2.0", "org.opencontainers.image.ref.name": "2.0"}}, "0" * 64
        with self.assertRaisesRegex(ValueError, "formula/version/revision mismatch"):
            lib.bottle_source(Registry(), {"formula": "demo", "keg": "1.0", "version": "1.0", "revision": 0})

    def test_newer_macos_binary_is_rejected(self):
        def output(*args):
            if args[0].endswith("lipo"):
                return "arm64\n"
            return "Load command 0\n cmd LC_BUILD_VERSION\n platform MACOS\n minos 26.0\n"
        with patch.object(lib, "run", side_effect=output), self.assertRaisesRegex(ValueError, "newer than macOS 15"):
            lib.macho(Path("unused"))

    def test_stage_verification_failure_keeps_original_files(self):
        with tempfile.TemporaryDirectory() as folder:
            frameworks = Path(folder) / "Frameworks"
            frameworks.mkdir()
            original = frameworks / "libdemo.dylib"
            original.write_bytes(b"original")
            installed = {"formula": "demo", "keg": "1.0", "local_path": "/brew/libdemo.dylib"}
            with patch.object(lib, "origin", return_value=installed), patch.object(lib, "bottle_source", side_effect=ValueError("no exact bottle")):
                with self.assertRaisesRegex(ValueError, "no exact bottle"):
                    lib.stage(frameworks, Path(folder) / "cache", None)
            self.assertEqual(original.read_bytes(), b"original")
            self.assertEqual([p.name for p in Path(folder).iterdir()], ["Frameworks"])


if __name__ == "__main__":
    unittest.main()
