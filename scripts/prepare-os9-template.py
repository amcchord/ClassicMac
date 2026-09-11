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
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import tempfile
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
GRAPHICS_EXTENSIONS = (
    "QuickDraw™ 3D", "QuickDraw™ 3D IR", "QuickDraw™ 3D RAVE",
    "QuickDraw™ 3D Viewer", "QD3DCustomElements", "OpenGLEngine",
    "OpenGLLibrary", "OpenGLMemory", "OpenGLRenderer", "OpenGLUtility",
)


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


def wrapper_geometry(source: Path, capacity_bytes: int) -> dict[str, int]:
    """Scale Drive Setup's HFS boot wrapper without changing its block IDs.

    HFS uses 16-bit allocation block numbers. Keeping those IDs and scaling
    the allocation block size preserves its boot files and bad-block extent,
    while the independently formatted HFS+ volume gets the requested space.
    See Apple TN1150, HFS Wrapper (drAlBlkSiz and drEmbedExtent).
    """
    source_start, source_size = partition(source)
    with source.open("rb") as original:
        original.seek(source_start + 1024)
        wrapper = original.read(512)
    if wrapper[:2] != b"BD" or wrapper[124:126] != b"H+":
        raise ValueError("Source must contain a classic HFS Plus boot wrapper")
    old_block_size = struct.unpack_from(">I", wrapper, 20)[0]
    allocation_start = struct.unpack_from(">H", wrapper, 28)[0] * 512
    total_blocks = struct.unpack_from(">H", wrapper, 18)[0]
    embedded_start, embedded_blocks = struct.unpack_from(">HH", wrapper, 126)
    trailing_sectors = (source.stat().st_size - source_start - source_size) // 512
    if (not 0 < old_block_size <= 1024 ** 2 or old_block_size % 4096 or
            not 0 < allocation_start <= 65536 or not 0 < embedded_start <= 16 or
            embedded_start + embedded_blocks != total_blocks or
            not 2 <= trailing_sectors <= 32768 or capacity_bytes % 512):
        raise ValueError("Unsupported classic wrapper geometry")
    partition_size = capacity_bytes - source_start - trailing_sectors * 512
    # Leave room for the HFS alternate MDB and align HFS+ allocations to 4 KiB.
    block_size = ((partition_size - allocation_start - 1024) // total_blocks) // 4096 * 4096
    if block_size < old_block_size or block_size > 4 * 1024 ** 2:
        raise ValueError("Requested capacity cannot preserve this boot wrapper")
    start = source_start + allocation_start + embedded_start * block_size
    length = embedded_blocks * block_size
    return dict(source_start=source_start, partition_size=partition_size,
                allocation_start=allocation_start, old_block_size=old_block_size,
                block_size=block_size, embedded_start=embedded_start,
                embedded_blocks=embedded_blocks, start=start, length=length,
                capacity_bytes=capacity_bytes)


def partition_prefix(source: Path, capacity_bytes: int, hfs_size: int) -> bytes:
    """Copy only driver partitions, updating the APM and final free extent."""
    start, _ = partition(source)
    if not 4 * 1024 ** 2 <= hfs_size < capacity_bytes - start or hfs_size % 512:
        raise ValueError("Invalid target HFS partition size")
    with source.open("rb") as file:
        prefix = bytearray(file.read(start))
    struct.pack_into(">I", prefix, 4, capacity_bytes // 512)
    count = struct.unpack_from(">I", prefix, 512 + 4)[0]
    found_hfs = found_free = False
    for index in range(1, count + 1):
        offset = index * 512
        kind = prefix[offset + 48:offset + 80].split(b"\0", 1)[0]
        entry_start = struct.unpack_from(">I", prefix, offset + 8)[0] * 512
        if kind == b"Apple_HFS":
            if found_hfs or entry_start != start:
                raise ValueError("Expected one HFS partition")
            found_hfs = True
            struct.pack_into(">I", prefix, offset + 12, hfs_size // 512)
            struct.pack_into(">I", prefix, offset + 84, hfs_size // 512)
        elif entry_start >= start:
            if kind != b"Apple_Free" or found_free:
                raise ValueError("Unexpected partition after source filesystem")
            found_free = True
            free_start = (start + hfs_size) // 512
            free_blocks = capacity_bytes // 512 - free_start
            struct.pack_into(">II", prefix, offset + 8, free_start, free_blocks)
            struct.pack_into(">I", prefix, offset + 84, free_blocks)
    if not found_hfs or not found_free:
        raise ValueError("Expected an HFS partition and trailing free partition")
    return bytes(prefix)


def resize_wrapper_catalog(data: bytes, old_size: int, new_size: int) -> bytes:
    """Update physical fork lengths after relocating the wrapper's blocks."""
    result = bytearray(data)
    node_size = struct.unpack_from(">H", data, 32)[0]
    if node_size != 512 or len(data) % node_size:
        raise ValueError("Unsupported HFS wrapper catalog")
    allowed = {b"Finder", b"System", b"Where_have_all_my_files_gone?",
               b"Desktop DB", b"Desktop DF"}
    seen = set()
    for offset in range(0, len(data), node_size):
        if data[offset + 8] != 0xff:  # Leaf node.
            continue
        records = struct.unpack_from(">H", data, offset + 10)[0]
        if records > 30:
            raise ValueError("Invalid HFS wrapper catalog node")
        for record in range(records):
            relative = struct.unpack_from(">H", data, offset + node_size - 2 * (record + 1))[0]
            key = offset + relative
            if not 14 <= relative < node_size - 8:
                raise ValueError("Invalid HFS wrapper catalog record")
            body = (key + 1 + data[key] + 1) & ~1
            if body + 102 > offset + node_size:
                raise ValueError("Truncated HFS wrapper catalog record")
            if data[body:body + 2] != b"\x02\x00":
                continue
            name = data[key + 7:key + 7 + data[key + 6]]
            if name not in allowed or name in seen:
                raise ValueError("Unexpected file in HFS boot wrapper")
            seen.add(name)
            for physical in (30, 40):
                length = struct.unpack_from(">I", data, body + physical)[0]
                if length % old_size:
                    raise ValueError("Unaligned HFS wrapper physical fork length")
                struct.pack_into(">I", result, body + physical, length // old_size * new_size)
    if seen != allowed:
        raise ValueError("Missing standard HFS boot wrapper files")
    return bytes(result)


def retain_classic_wrapper(source: Path, image: Path) -> None:
    """Rebuild a resized boot wrapper around the new, sparse HFS+ filesystem."""
    layout = wrapper_geometry(source, image.stat().st_size)
    source_start = layout["source_start"]
    old_block_size, block_size = layout["old_block_size"], layout["block_size"]
    allocation = source_start + layout["allocation_start"]
    start, length = layout["start"], layout["length"]
    with source.open("rb") as original, image.open("rb") as payload:
        original.seek(source_start)
        wrapper_prefix = bytearray(original.read(layout["allocation_start"]))
        wrapper = bytearray(wrapper_prefix[1024:1536])
        for field in (20, 24, 74, 78):
            old_value = struct.unpack_from(">I", wrapper, field)[0]
            if old_value % old_block_size:
                raise ValueError("Unexpected HFS wrapper clump size")
            struct.pack_into(">I", wrapper, field, old_value // old_block_size * block_size)
        wrapper_prefix[1024:1536] = wrapper
        payload_start, _ = partition(image)
        payload.seek(payload_start + 1024)
        header = payload.read(512)
        payload_size = struct.unpack_from(">I", header, 40)[0] * struct.unpack_from(">I", header, 44)[0]
        if header[:2] != b"H+" or payload_size > length:
            raise ValueError("New filesystem does not fit the classic wrapper")
        catalog_start, catalog_blocks = struct.unpack_from(">HH", wrapper, 150)
        if catalog_blocks != 1 or catalog_start >= layout["embedded_start"]:
            raise ValueError("Unsupported HFS wrapper catalog extent")
        descriptor, temporary_name = tempfile.mkstemp(prefix=".wrapped-disk-", suffix=".img", dir=image.parent)
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output:
                output.write(partition_prefix(source, layout["capacity_bytes"], layout["partition_size"]))
                output.write(wrapper_prefix)
                for block in range(layout["embedded_start"]):
                    original.seek(allocation + block * old_block_size)
                    data = original.read(old_block_size)
                    if block == catalog_start:
                        data = resize_wrapper_catalog(data, old_block_size, block_size)
                    output.seek(allocation + block * block_size)
                    output.write(data)
                output.seek(start)
                payload.seek(payload_start)
                remaining = payload_size
                while remaining:
                    chunk = payload.read(min(1024 ** 2, remaining))
                    if not chunk:
                        raise ValueError("Truncated new filesystem")
                    if chunk.count(0) == len(chunk):
                        output.seek(len(chunk), 1)
                    else:
                        output.write(chunk)
                    remaining -= len(chunk)
                # Both alternate headers belong at their actual volume ends,
                # including any gap after the formatter's allocation blocks.
                output.seek(start + length - 1024)
                output.write(header)
                output.seek(source_start + layout["partition_size"] - 1024)
                output.write(wrapper)
                output.truncate(layout["capacity_bytes"])
                output.flush()
                os.fsync(output.fileno())
            temporary.replace(image)
        finally:
            temporary.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, help="New .classic package")
    parser.add_argument("--guest-dir", type=Path, default=Path(__file__).resolve().parents[1] / "gxmetal/guest/bin")
    parser.add_argument("--graphics-source", type=Path,
                        help="Read-only OS 9 image containing the complete Apple QuickDraw 3D/OpenGL components")
    parser.add_argument("--capacity-gb", type=int, choices=(8, 16, 32, 64, 128), default=32,
                        help="Sparse virtual disk capacity in GiB (default: 32)")
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.exists() or output.suffix != ".classic":
        parser.error("Output must be a new .classic package")
    layout = wrapper_geometry(source, args.capacity_gb * 1024 ** 3)
    source_before = (source.stat().st_size, source.stat().st_mtime_ns)
    source_device = target_device = graphics_device = None
    output.mkdir(parents=True)
    disk = output / "disk.img"
    inventory = []
    try:
        # Fresh sparse zero-filled image, retaining only standard boot drivers.
        with disk.open("xb") as target:
            target.write(partition_prefix(source, layout["capacity_bytes"], layout["length"]))
            target.truncate(layout["capacity_bytes"])
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
        # Format the exact prepared extent. eraseVolume silently reserves an
        # additional 128 MiB and rewrites the APM on current macOS versions.
        raw_volume = target_volume.replace("/dev/disk", "/dev/rdisk", 1)
        run("/sbin/newfs_hfs", "-v", "Macintosh HD", raw_volume)
        run("diskutil", "mount", target_volume)
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

        graphics_root = source_root
        if args.graphics_source:
            graphics_device, graphics_volume = attach(args.graphics_source.resolve(), True)
            run("diskutil", "mount", "readOnly", graphics_volume)
            graphics_root = mounted_path(graphics_volume)
        graphics_inventory = []
        for name in GRAPHICS_EXTENSIONS:
            origin = graphics_root / "System Folder/Extensions" / name
            if not origin.is_file() or origin.is_symlink():
                raise ValueError(f"Missing Apple graphics component {name}; supply a complete --graphics-source")
            resource = Path(str(origin) + "/..namedfork/rsrc").read_bytes()
            if b"Apple Computer" not in resource:
                raise ValueError(f"Expected an Apple-supplied system component: {name}")
            copy_item(origin, target_root / "System Folder/Extensions" / name)
            graphics_inventory.append({
                "name": name,
                "dataSHA256": hashlib.sha256(origin.read_bytes()).hexdigest(),
                "resourceSHA256": hashlib.sha256(resource).hexdigest(),
            })

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
        retain_classic_wrapper(source, disk)
        config = {
            "id": str(uuid.uuid4()), "name": f"Mac OS {os_version}",
            "machineFamily": "powerMacG4", "ramMB": 512,
            "diskImageName": "disk.img", "pramImageName": "pram.img",
            "diskSizeGB": args.capacity_gb,
            "width": 1024, "height": 768, "depth": 16,
            "useEnhancedFramebuffer": False, "customResolution": False,
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
            "diskCapacityBytes": disk.stat().st_size,
            "allocatedBytes": disk.stat().st_blocks * 512,
            "hfsPlusCapacityBytes": layout["length"],
            "appleGraphicsComponents": graphics_inventory,
        }, indent=2) + "\n")
        print(f"Prepared {output} (Mac OS {os_version}); runtime validation required.")
    finally:
        for device in (target_device, graphics_device, source_device):
            if device:
                subprocess.run(["diskutil", "eject", device], check=False,
                               stdout=subprocess.DEVNULL)
        if (source.stat().st_size, source.stat().st_mtime_ns) != source_before:
            raise RuntimeError("Source image metadata changed")


if __name__ == "__main__":
    main()
