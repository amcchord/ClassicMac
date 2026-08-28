#!/usr/bin/env python3
"""Generate the Rez source that makes ClassicMac additions mandatory.

The stock Mac OS 9.2.1 Installer's hidden package 5000 is a child of every
Core System Software choice. Replacing that package with an otherwise exact
copy that also references package 24000 makes the additions run in both Easy
Install and Custom Install.

Each staged file becomes a standard Installer 4 format-1 file atom. This is
the same mechanism Apple's Aladdin and Netscape packages on the 9.2.1 CD use,
and it lets Installer create the Apple Extras hierarchy as it copies files.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import struct
import unicodedata
from dataclasses import dataclass
from pathlib import Path


PACKAGE_ID = 24000
ATOM_ID_BASE = 24100
SOURCE_ID_BASE = 25000
TARGET_ID_BASE = 26000
MAC_EPOCH_OFFSET = 2082844800
LIBC = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)


@dataclass(frozen=True)
class InstallFile:
    source: Path
    source_path: str
    target_path: str
    file_type: bytes
    creator: bytes
    finder_flags: int
    created: int
    modified: int
    data_size: int
    resource_size: int

    @property
    def total_size(self) -> int:
        return self.data_size + self.resource_size


def resource_fork_size(path: Path) -> int:
    try:
        return os.stat(str(path) + "/..namedfork/rsrc").st_size
    except OSError:
        return 0


def finder_info(path: Path) -> bytes:
    buffer = ctypes.create_string_buffer(32)
    length = LIBC.getxattr(
        os.fsencode(path), b"com.apple.FinderInfo", buffer, 32, 0, 0
    )
    return buffer.raw if length == 32 else bytes(32)


def hfs_name(name: str) -> str:
    """Apply the same Mac-Roman conversion and 31-byte limit as hfs-copy.py."""
    normalized = unicodedata.normalize("NFC", name).replace(":", "/")
    encoded = normalized.encode("mac_roman", errors="replace")[:31]
    return encoded.decode("mac_roman")


def hfs_relative(path: Path) -> str:
    return ":".join(hfs_name(part) for part in path.parts)


def ostype(value: bytes) -> str:
    if value == bytes(4):
        return "''"
    text = value.decode("mac_roman")
    return "'" + text.replace("\\", "\\\\").replace("'", "\\'") + "'"


def rez_string(value: str) -> str:
    value.encode("mac_roman")
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    # Classic custom-icon files are literally named "Icon" + carriage return.
    # DeRez spells that Mac-Roman 0x0D byte as \n, so use the same source form.
    escaped = escaped.replace("\r", "\\n").replace("\n", "\\n")
    return '"' + escaped + '"'


def staged_files(stage: Path, source_volume: str) -> list[InstallFile]:
    mappings = (
        ("System Extensions", "special-extX:"),
        ("Control Panels", "special-ctrX:"),
        ("Apple Extras", "special-root:Apple Extras:"),
    )
    result: list[InstallFile] = []
    source_paths: set[str] = set()
    target_paths: set[str] = set()

    for source_folder, target_prefix in mappings:
        root = stage / source_folder
        if not root.is_dir():
            raise SystemExit(f"missing staged folder: {root}")

        for directory, names, filenames in os.walk(root):
            names[:] = sorted(name for name in names if not name.startswith("."))
            visible_files = sorted(name for name in filenames if not name.startswith("."))
            for name in visible_files:
                path = Path(directory) / name
                relative = path.relative_to(root)
                mac_relative = hfs_relative(relative)
                info = finder_info(path)
                stats = path.stat()
                timestamp = int(stats.st_mtime) + MAC_EPOCH_OFFSET
                item = InstallFile(
                        source=path,
                        source_path=(
                            f"{source_volume}:ClassicMac Additions:"
                            f"{source_folder}:{mac_relative}"
                        ),
                        target_path=target_prefix + mac_relative,
                        file_type=info[0:4],
                        creator=info[4:8],
                        finder_flags=struct.unpack(">H", info[8:10])[0] & ~0x0100,
                        created=timestamp,
                        modified=timestamp,
                        data_size=stats.st_size,
                        resource_size=resource_fork_size(path),
                    )
                if item.source_path.casefold() in source_paths:
                    raise SystemExit(
                        f"HFS source-path collision after Mac-Roman/31-byte conversion: "
                        f"{item.source_path}"
                    )
                if item.target_path.casefold() in target_paths:
                    raise SystemExit(
                        f"HFS target-path collision after Mac-Roman/31-byte conversion: "
                        f"{item.target_path}"
                    )
                source_paths.add(item.source_path.casefold())
                target_paths.add(item.target_path.casefold())
                result.append(item)

    if not result:
        raise SystemExit("the staging tree contains no files")
    if len(result) >= 700:
        raise SystemExit("too many files for the reserved Installer resource IDs")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-volume", default="Mac OS 9.2.1")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    files = staged_files(args.stage, args.source_volume)

    package_parts = "\n".join(
        f"            'infa', {ATOM_ID_BASE + index}{',' if index + 1 < len(files) else ''}"
        for index in range(len(files))
    )

    resources = [
        "#include \"InstallerTypes.r\"",
        "",
        "/* Stock hidden package 5000, plus the mandatory ClassicMac package. */",
        "resource 'inpk' (5000) {",
        "    format0 {",
        "        doesntShowOnCustom,",
        "        notRemovable,",
        "        forceRestart,",
        "        0,",
        "        7021635,",
        "        \"\",",
        "        {",
        "            'inrm', 7000,",
        "            'inbb', 14000,",
        "            'inra', 16000,",
        "            'inra', 16001,",
        "            'inra', 16002,",
        "            'inra', 16003,",
        "            'inaa', 15016,",
        f"            'inpk', {PACKAGE_ID}",
        "        }",
        "    }",
        "};",
        "",
        f"resource 'inpk' ({PACKAGE_ID}) {{",
        "    format0 {",
        "        doesntShowOnCustom,",
        "        notRemovable,",
        "        forceRestart,",
        "        0,",
        f"        {sum(item.total_size for item in files)},",
        "        \"\",",
        "        {",
        package_parts,
        "        }",
        "    }",
        "};",
        "",
    ]

    for index, item in enumerate(files):
        atom_id = ATOM_ID_BASE + index
        source_id = SOURCE_ID_BASE + index
        target_id = TARGET_ID_BASE + index
        resources.extend(
            [
                f"resource 'infs' ({source_id}) {{",
                f"    {ostype(item.file_type)},",
                f"    {ostype(item.creator)},",
                "    0x0,",
                "    noSearchForFile,",
                "    TypeCrMustMatch,",
                f"    {rez_string(item.source_path)}",
                "};",
                "",
                f"resource 'intf' ({target_id}) {{",
                "    format1 {",
                "        noSearchForFile,",
                "        TypeCrNeedNotMatch,",
                f"        {ostype(item.file_type)},",
                f"        {ostype(item.creator)},",
                f"        0x{item.finder_flags:04X},",
                f"        0x{item.created:08X},",
                f"        0x{item.modified:08X},",
                "        0,",
                f"        {rez_string(item.target_path)}",
                "    }",
                "};",
                "",
                f"resource 'infa' ({atom_id}) {{",
                "    format1 {",
                "        deleteWhenRemoving,",
                "        dontDeleteWhenInstalling,",
                "        copy,",
                "        dontIgnoreLockedFile,",
                "        dontSetFileLocked,",
                "        useSrcCrDateToCompare,",
                "        srcNeedExist,",
                "        rsrcForkInRsrcFork,",
                "        updateEvenIfNewer,",
                "        updateExisting,",
                "        copyIfNewOrUpdate,",
                "        rsrcFork,",
                "        dataFork,",
                f"        {item.total_size},",
                f"        {item.finder_flags},",
                f"        {target_id},",
                "        {",
                f"            {source_id}, {item.data_size}, {item.resource_size}",
                "        },",
                "        0x0,",
                "        0,",
                "        0,",
                f"        {rez_string(item.target_path.rsplit(':', 1)[-1])}",
                "    }",
                "};",
                "",
            ]
        )

    args.output.write_bytes("\n".join(resources).encode("mac_roman"))
    print(
        f"generated {args.output}: {len(files)} file atoms, "
        f"{sum(item.total_size for item in files)} bytes"
    )


if __name__ == "__main__":
    main()
