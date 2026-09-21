#!/usr/bin/env python3
"""Package a stopped, scrubbed Power Mac into ClassicMac's constrained tar format.

This never changes the source machine and never uploads anything. Guest disk
scrubbing/qualification must happen before this command; it cannot remove user
files or personal information inside HFS volumes. Output paths must be new.
"""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import tarfile
import tempfile
from urllib.parse import urlsplit


# Storage accounting must match MachineTemplateInstaller.sparseChunkBytes.
SPARSE_CHUNK_BYTES = 1_048_576


def storage_charge(size):
    return ((size + SPARSE_CHUNK_BYTES - 1) // SPARSE_CHUNK_BYTES) * SPARSE_CHUNK_BYTES


class SparseAccountingReader:
    """Account at Swift's 1 MiB chunk boundaries using the exact archived bytes."""
    def __init__(self, source):
        self.source = source
        self.chunk_bytes = 0
        self.chunk_nonzero = False
        self.completed_storage = 0
        self.bytes_read = 0

    def read(self, size=-1):
        data = self.source.read(size)
        self.bytes_read += len(data)
        offset = 0
        while offset < len(data):
            length = min(len(data) - offset, SPARSE_CHUNK_BYTES - self.chunk_bytes)
            if not self.chunk_nonzero:
                self.chunk_nonzero = data[offset:offset + length].count(0) != length
            self.chunk_bytes += length
            offset += length
            if self.chunk_bytes == SPARSE_CHUNK_BYTES:
                self.completed_storage += SPARSE_CHUNK_BYTES if self.chunk_nonzero else 0
                self.chunk_bytes = 0
                self.chunk_nonzero = False
        return data

    @property
    def required_storage(self):
        return self.completed_storage + (SPARSE_CHUNK_BYTES if self.chunk_nonzero else 0)


def secure_url(value):
    url = urlsplit(value)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.fragment:
        raise argparse.ArgumentTypeError("Use an HTTPS URL without credentials or a fragment")
    return value


def regular_file(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    file = os.fdopen(descriptor, "rb")
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        file.close()
        raise ValueError(f"Not a regular file: {path.name}")
    return file


def header(name, size):
    info = tarfile.TarInfo(name)
    info.size = size
    info.mode = 0o600
    info.uid = info.gid = info.mtime = 0
    info.uname = info.gname = ""
    return info


def publish_new_file(staged, destination):
    # A hard link is an atomic, non-replacing publication on this same volume.
    os.link(staged, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New .tar.gz archive")
    parser.add_argument("--catalog-output", type=Path, required=True, help="New catalog JSON")
    parser.add_argument("--archive-url", type=secure_url, required=True)
    parser.add_argument("--id", required=True, help="Immutable template version, e.g. mac-os-9.2.1-gxmetal-2.3.0-v1")
    parser.add_argument("--name", default="Mac OS 9")
    parser.add_argument("--summary")
    parser.add_argument("--os-version", required=True)
    parser.add_argument("--gxmetal-version", required=True)
    parser.add_argument("--minimum-app-version", default="3.0.0")
    parser.add_argument("--include-preview", action="store_true", help="Include the scrubbed machine's preview.png")
    args = parser.parse_args()
    args.summary = args.summary or f"{args.os_version} with GXMetal installed and ready to start."
    if not re.fullmatch(r"[a-z0-9][a-z0-9._-]{0,99}", args.id):
        parser.error("id must be 1–100 lowercase letters, digits, dots, hyphens, or underscores")
    if not re.fullmatch(r"[0-9]{1,6}\.[0-9]{1,6}(\.[0-9]{1,6})?", args.minimum_app_version):
        parser.error("minimum-app-version must be a numeric release such as 3.0.0")
    for text, maximum in [(args.name, 100), (args.summary, 1000), (args.os_version, 80), (args.gxmetal_version, 80)]:
        if not text.strip() or len(text) > maximum or any(ord(c) < 32 and c != "\n" for c in text):
            parser.error("Invalid or overly long catalog text")
    bundle = args.bundle.resolve(strict=True)
    output = args.output.absolute()
    catalog_output = args.catalog_output.absolute()
    if output == catalog_output:
        parser.error("Archive and catalog must use different output paths")
    for path in (output, catalog_output):
        if path.exists() or path.is_symlink():
            parser.error(f"Output already exists: {path}")
        path.parent.mkdir(parents=True, exist_ok=True)
    with regular_file(bundle / "config.json") as file:
        if os.fstat(file.fileno()).st_size > 65_536:
            parser.error("config.json must be at most 64 KiB")
        source = json.load(file)
    if source.get("machineFamily") not in ("powerMacG4", "powerMac7500") or source.get("diskImageName", "disk.img") != "disk.img":
        parser.error("Only Power Mac templates with disk.img are supported")

    copland = source.get("machineFamily") == "powerMac7500"
    if copland:
        version = tuple(map(int, args.minimum_app_version.split(".")))
        if version < (3, 2, 0):
            parser.error("Copland templates need ClassicMac 3.2.0 or later")
        for filename, size in [("bootrom.bin", 4_194_304), ("nvram.bin", 8209)]:
            with regular_file(bundle / filename) as file:
                if os.fstat(file.fileno()).st_size != size:
                    parser.error(f"Invalid {filename} size")
                data = file.read()
                if filename == "bootrom.bin" and hashlib.sha256(data).hexdigest() != "098b588dbe12fdfa3d388636e431ccae69cd1c6e984801267b9b2602babbfd22":
                    parser.error("Copland needs the matched Power Mac 7500 ROM")
                if filename == "nvram.bin" and data[:17] != b"DINGUSPPCNVRAM\x00\x00\x20":
                    parser.error("Invalid Copland NVRAM header")

    # Persist known hardware settings only. No developer paths or mounted media
    # are copied to the downloadable config; import sanitizes these again.
    allowed = {"ramMB", "diskSizeGB", "width", "height", "depth", "customResolution",
               "networking", "sound", "useG4CPU", "tabletInput", "classicInputHelpers"}
    config = {key: value for key, value in source.items() if key in allowed}
    config.update(id="00000000-0000-0000-0000-000000000000", name=args.name,
                  machineFamily=source["machineFamily"], diskImageName="disk.img", pramImageName="pram.img",
                  useEnhancedFramebuffer=False, useBrowserDisplay=False, bootFromCD=False,
                  toolsCDInserted=True, toolsDeliveryVersion=1)
    if copland:
        config.update(ramMB=32, width=640, height=480, depth=8, customResolution=False,
                      networking=False, sound=False, useG4CPU=False, tabletInput=False,
                      classicInputHelpers=False, toolsCDInserted=False)
    config_bytes = json.dumps(config, indent=2, sort_keys=True).encode() + b"\n"
    installed_bytes = len(config_bytes)
    required_storage_bytes = storage_charge(len(config_bytes))
    disk_capacity_bytes = None
    staged = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".classicmac-template-", dir=output.parent, delete=False) as temp:
            staged = Path(temp.name)
            with gzip.GzipFile(filename="", fileobj=temp, mode="wb", compresslevel=6, mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.GNU_FORMAT) as archive:
                    archive.addfile(header("config.json", len(config_bytes)), io.BytesIO(config_bytes))
                    for name in ["disk.img"] + (["bootrom.bin", "nvram.bin"] if copland else []) + (["preview.png"] if args.include_preview else []):
                        with regular_file(bundle / name) as file:
                            before = os.fstat(file.fileno())
                            if name == "disk.img" and (before.st_size < 512 or before.st_size % 512):
                                raise ValueError("disk.img must be a raw, sector-aligned disk")
                            if name == "preview.png" and not (0 < before.st_size <= 16 * 1_048_576):
                                raise ValueError("preview.png must be at most 16 MB")
                            installed_bytes += before.st_size
                            if installed_bytes > 140 * 1_073_741_824:
                                raise ValueError("Template exceeds the supported 140 GB limit")
                            if name == "disk.img":
                                disk_capacity_bytes = before.st_size
                                reader = SparseAccountingReader(file)
                                archive.addfile(header(name, before.st_size), reader)
                                if reader.bytes_read != before.st_size:
                                    raise ValueError("The source disk was truncated while packaging")
                                required_storage_bytes += reader.required_storage
                            else:
                                archive.addfile(header(name, before.st_size), file)
                                required_storage_bytes += storage_charge(before.st_size)
                            after = os.fstat(file.fileno())
                            if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                                raise ValueError("The source changed while packaging. Shut down the Mac and try again.")
            temp.flush()
            os.fsync(temp.fileno())
        digest = hashlib.sha256()
        with staged.open("rb") as file:
            for chunk in iter(lambda: file.read(1_048_576), b""):
                digest.update(chunk)
        entry = dict(id=args.id, name=args.name, summary=args.summary, osVersion=args.os_version,
                     gxMetalVersion=args.gxmetal_version, minimumAppVersion=args.minimum_app_version,
                     archiveURL=args.archive_url, archiveBytes=staged.stat().st_size,
                     installedBytes=installed_bytes, diskCapacityBytes=disk_capacity_bytes,
                     requiredStorageBytes=required_storage_bytes, sha256=digest.hexdigest())
        if copland:
            entry["machineFamily"] = "powerMac7500"
        os.chmod(staged, 0o644)
        publish_new_file(staged, output)
        try:
            with catalog_output.open("x", encoding="utf-8") as file:
                json.dump(dict(schemaVersion=1, machines=[entry]), file, indent=2)
                file.write("\n")
        except Exception:
            # Keep the completed archive for inspection if catalog publication
            # fails; never delete a path another process may now be using.
            raise
        print(f"Archive: {output}")
        print(f"Catalog: {catalog_output}")
        print(f"Download: {entry['archiveBytes']:,} bytes; expanded: {installed_bytes:,} bytes")
        print(f"Guest disk: {disk_capacity_bytes:,} bytes; initial storage bound: {required_storage_bytes:,} bytes")
        print(f"SHA-256: {entry['sha256']}")
    finally:
        if staged is not None:
            staged.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
