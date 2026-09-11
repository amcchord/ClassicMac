#!/usr/bin/env python3
"""Assemble a fresh public-template disk from an audited OS 9 installation.

The source is attached read-only. Only the Apple disk-driver/partition prefix
and explicitly selected system/application files are copied. The filesystem
is newly formatted: deleted source files and source free space never enter the
output. Preferences, documents, caches, keychains, recent items, and source
machine settings are excluded. Run only against an installation whose system
and application files have been reviewed for distribution.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import uuid


SYSTEM_ITEMS = (
    "Appearance", "Application Support", "ColorSync Profiles",
    "Contextual Menu Items", "Control Panels", "Control Strip Modules",
    "Extensions", "Finder", "Fonts", "Help", "Internet Search Sites",
    "Language & Region Support", "Launcher Items", "Login", "Mac OS ROM",
    "MacTCP DNR", "Panels", "Scripting Additions", "Scripts", "System",
    "System Resources", "Text Encodings",
)
APPLE_MENU_ITEMS = (
    "Apple System Profiler", "Calculator", "Chooser", "Control Panels",
    "Key Caps", "Network Browser", "Sherlock 2", "Stickies",
)
APPLICATIONS = ("SimpleText", "Graphing Calculator")


def run(*args: str) -> str:
    result = subprocess.run(args, check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    return result.stdout.decode("utf-8", "replace")


def partition(image: Path) -> tuple[int, int]:
    with image.open("rb") as f:
        header = f.read(512)
        if header[:2] != b"ER" or struct.unpack_from(">H", header, 2)[0] != 512:
            raise ValueError("A 512-byte Apple Driver Map is required")
        f.seek(512)
        first = f.read(512)
        count = struct.unpack_from(">I", first, 4)[0]
        if first[:2] != b"PM" or not 1 <= count <= 64:
            raise ValueError("Invalid Apple partition map")
        for index in range(1, count + 1):
            f.seek(index * 512)
            entry = f.read(512)
            if entry[:2] != b"PM":
                raise ValueError("Invalid partition entry")
            if entry[48:80].split(b"\0", 1)[0] == b"Apple_HFS":
                start, blocks = struct.unpack_from(">II", entry, 8)
                if not 64 <= start <= 1048576 or blocks < 8192 or (start + blocks) * 512 > image.stat().st_size:
                    raise ValueError("Unexpected partition geometry")
                return start * 512, blocks * 512
    raise ValueError("No HFS partition")


def attach(image: Path, read_only: bool) -> tuple[str, str]:
    args = ["diskutil", "image", "attach", "--noMount"]
    if read_only:
        args.append("--readOnly")
    lines = run(*args, str(image)).splitlines()
    device = lines[0].split()[0]
    volume = next(line.split()[0] for line in lines if "Apple_HFS" in line)
    if not re.fullmatch(r"/dev/disk\d+", device) or not volume.startswith(device + "s"):
        raise ValueError("Unexpected attached image device")
    return device, volume


def mounted_path(volume: str) -> Path:
    info = plistlib.loads(subprocess.check_output(["diskutil", "info", "-plist", volume]))
    return Path(info["MountPoint"])


def hfs_partition(device: str) -> str:
    listing = plistlib.loads(subprocess.check_output(["diskutil", "list", "-plist", device]))
    parts = listing["AllDisksAndPartitions"][0]["Partitions"]
    matches = [p["DeviceIdentifier"] for p in parts if p.get("Content") == "Apple_HFS"]
    if len(matches) != 1 or not matches[0].startswith(device.removeprefix("/dev/") + "s"):
        raise ValueError("Expected exactly one HFS partition on output image")
    return "/dev/" + matches[0]


def copy_item(source: Path, destination: Path) -> None:
    if source.is_symlink():
        raise ValueError(f"Symbolic link in template source: {source.name}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    run("ditto", "--rsrc", "--extattr", "--noacl", "--noqtn", str(source), str(destination))


def bless_hfsplus(image: Path, start: int, size: int, folder_id: int) -> None:
    # TN1150: finderInfo[0]/[1]/[3] identify the System Folder, startup
    # application's parent, and classic System Folder. Update both headers.
    with image.open("r+b") as f:
        for offset in (start + 1024, start + size - 1024):
            f.seek(offset)
            header = bytearray(f.read(512))
            if header[:2] != b"H+":
                raise ValueError("Expected a plain HFS Plus filesystem")
            for index in (0, 1, 3):
                struct.pack_into(">I", header, 80 + index * 4, folder_id)
            f.seek(offset)
            f.write(header)
        f.flush()
        os.fsync(f.fileno())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New .classic package")
    parser.add_argument("--guest-dir", type=Path, default=Path(__file__).resolve().parents[1] / "gxmetal/guest/bin")
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.exists() or output.suffix != ".classic":
        parser.error("Output must be a new .classic package")
    start, size = partition(source)
    source_before = (source.stat().st_size, source.stat().st_mtime_ns)
    source_device = target_device = None
    output.mkdir(parents=True)
    disk = output / "disk.img"
    inventory = []
    try:
        # Fresh sparse zero-filled image, retaining only standard boot drivers.
        with source.open("rb") as original, disk.open("xb") as target:
            target.write(original.read(start))
            target.truncate(source.stat().st_size)
        source_device, source_volume = attach(source, True)
        run("diskutil", "mount", "readOnly", source_volume)
        source_root = mounted_path(source_volume)
        system = source_root / "System Folder"
        if not all((system / name).exists() for name in ("System", "Finder", "Mac OS ROM")):
            raise ValueError("Source does not contain a PowerPC Mac OS installation")
        version_resource = run("/usr/bin/DeRez", "-only", "vers", str(system / "System"))
        match = re.search(r'\$"([0-9A-Fa-f]{4})', version_resource)
        if not match or not match[1].startswith("09"):
            raise ValueError("Source is not Mac OS 9")
        os_version = f"9.{int(match[1][2],16)}.{int(match[1][3],16)}"

        target_device, target_volume = attach(disk, False)
        # This device was just attached from the new output image above.
        run("diskutil", "eraseVolume", "HFS+", "Macintosh HD", target_volume)
        # Reformatting an HFS wrapper can renumber the APM slice.
        target_volume = hfs_partition(target_device)
        target_root = mounted_path(target_volume)
        (target_root / ".metadata_never_index").touch()
        for name in SYSTEM_ITEMS:
            origin = system / name
            if origin.exists():
                copy_item(origin, target_root / "System Folder" / name)
                inventory.append("System Folder/" + name)
        for name in APPLE_MENU_ITEMS:
            origin = system / "Apple Menu Items" / name
            if origin.exists():
                copy_item(origin, target_root / "System Folder/Apple Menu Items" / name)
                inventory.append("System Folder/Apple Menu Items/" + name)
        for name in APPLICATIONS:
            origin = source_root / "Applications (Mac OS 9)" / name
            if origin.exists():
                copy_item(origin, target_root / "Applications (Mac OS 9)" / name)
                inventory.append("Applications (Mac OS 9)/" + name)
        for name in ("Preferences", "Startup Items", "Shutdown Items", "Servers", "Favorites"):
            (target_root / "System Folder" / name).mkdir(exist_ok=True)
        for name in ("Desktop Folder", "Documents", "Trash"):
            (target_root / name).mkdir(exist_ok=True)

        guest = args.guest_dir.resolve()
        for source_name, destination_name in (
            ("GXMetal", "GXMetal"), ("GXMetal Input", "GXMetal Input"),
            ("GXMetal Startup", "GXMetal Startup"),
        ):
            copy_item(guest / source_name, target_root / "System Folder/Extensions" / destination_name)
        for source_name, destination_name in (
            ("GXMetalTest", "GXMetal Test"),
            ("GXMetal AGL Probe", "GXMetal AGL Probe"),
            ("GXMetal RAVE Selection", "GXMetal RAVE Selection"),
            ("GXMetalInstaller", "Install GXMetal"),
        ):
            copy_item(guest / source_name, target_root / "ClassicMac Utilities" / destination_name)
        readme = target_root / "Desktop Folder/Start Here"
        readme.write_bytes((
            f"Welcome to ClassicMac\r\rMac OS {os_version} is ready to use.\r\r"
            "GXMetal and its input companion are already installed. Open ClassicMac Utilities "
            "on Macintosh HD to run GXMetal Test or the OpenGL probe.\r\r"
            "Use ClassicMac's media menu to attach a disc image. Power Mac disc changes "
            "may need a shutdown and restart before Mac OS sees them.\r\r"
            "Paste Text into Mac types plain text into the focused guest application. "
            "Open SimpleText in Applications (Mac OS 9) to try it.\r\r"
            "Choose a host folder in Settings to share your own files. No host folders "
            "are attached by this template.\r\r"
            "Shut down Mac OS before copying or moving this machine.\r"
        ).encode("mac_roman"))
        run("xattr", "-wx", "com.apple.FinderInfo", "5445585474747874" + "00" * 24, str(readme))
        folder_id = (target_root / "System Folder").stat().st_ino
        run("sync")
        run("diskutil", "eject", target_device)
        target_device = None
        start, size = partition(disk)
        bless_hfsplus(disk, start, size, folder_id)
        config = {
            "id": str(uuid.uuid4()), "name": f"Mac OS {os_version}",
            "machineFamily": "powerMacG4", "ramMB": 512,
            "diskImageName": "disk.img", "pramImageName": "pram.img",
            "diskSizeGB": source.stat().st_size // (1024 ** 3),
            "width": 1024, "height": 768, "depth": 16,
            "useEnhancedFramebuffer": True, "customResolution": False,
            "useBrowserDisplay": False, "bootFromCD": False,
            "toolsCDInserted": True, "toolsDeliveryVersion": 1,
            "networking": True, "sound": True, "useG4CPU": True,
            "tabletInput": True, "classicInputHelpers": True,
        }
        (output / "config.json").write_text(json.dumps(config, indent=2) + "\n")
        (output.parent / (output.stem + "-preparation.json")).write_text(json.dumps({
            "osVersion": os_version, "sourceFilesystemNotCloned": True,
            "copiedSystemItems": inventory, "systemFolderCNID": folder_id,
            "preferencesCopied": False, "documentsCopied": False,
            "sourceFreeSpaceCopied": False,
        }, indent=2) + "\n")
        print(f"Prepared {output} (Mac OS {os_version}); runtime validation required.")
    finally:
        for device in (target_device, source_device):
            if device:
                subprocess.run(["diskutil", "eject", device], check=False,
                               stdout=subprocess.DEVNULL)
        if (source.stat().st_size, source.stat().st_mtime_ns) != source_before:
            raise RuntimeError("Source image metadata changed")


if __name__ == "__main__":
    main()
