#!/usr/bin/env python3
"""Extract USB Overdrive 1.4's HBINTGHT installer payload as MacBinary II.

The public archive contains a small custom installer rather than ordinary
files.  Its FILE records describe Finder metadata and fork sizes; DUMP records
contain zlib streams.  MacBinary output lets the normal macOS and hfsutils
tools restore both forks without teaching the CD builder about HFS internals.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


EXPECTED_NAMES = {
    "USB Joystick Overdrive",
    "USB Mouse Overdrive",
    "USB Overdrive",
}


@dataclass(frozen=True)
class Entry:
    name: str
    file_type: bytes
    creator: bytes
    finder_flags: int
    created: int
    modified: int
    data: bytes
    resource: bytes


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def parse_dump(blob: bytes, offset: int) -> tuple[bytes, int]:
    if blob[offset : offset + 4] != b"DUMP":
        raise ValueError(f"expected DUMP record at 0x{offset:x}")
    record_size = u32(blob, offset + 4)
    uncompressed_size = u32(blob, offset + 8)
    start = offset + 12
    end = offset + record_size
    if end > len(blob):
        raise ValueError("truncated DUMP record")
    payload = zlib.decompress(blob[start:end])
    if len(payload) != uncompressed_size:
        raise ValueError("DUMP size mismatch")
    return payload, end


def parse_installer(path: Path) -> list[Entry]:
    blob = path.read_bytes()
    if not blob.startswith(b"HBINTGHT"):
        raise ValueError("not an HBINTGHT installer")

    file_offsets: list[int] = []
    cursor = 0
    while True:
        cursor = blob.find(b"FILE", cursor)
        if cursor < 0:
            break
        if cursor + 76 <= len(blob):
            record_size = u32(blob, cursor + 4)
            name_length = blob[cursor + 8]
            if 76 <= record_size and name_length <= 31:
                file_offsets.append(cursor)
        cursor += 4

    entries: list[Entry] = []
    for index, offset in enumerate(file_offsets):
        name_length = blob[offset + 8]
        name = blob[offset + 9 : offset + 9 + name_length].decode("mac_roman")
        data_size = u32(blob, offset + 68)
        resource_size = u32(blob, offset + 72)
        record_size = u32(blob, offset + 4)
        if record_size != 76 + data_size + resource_size:
            raise ValueError(f"uncompressed FILE size mismatch for {name}")

        cursor = offset + 76
        forks = bytearray()
        while len(forks) < data_size + resource_size:
            dump, cursor = parse_dump(blob, cursor)
            forks.extend(dump)
        if len(forks) != data_size + resource_size:
            raise ValueError(f"fork or record length mismatch for {name}")

        entries.append(
            Entry(
                name=name,
                file_type=blob[offset + 44 : offset + 48],
                creator=blob[offset + 48 : offset + 52],
                finder_flags=struct.unpack_from(">H", blob, offset + 52)[0],
                created=u32(blob, offset + 60),
                modified=u32(blob, offset + 64),
                data=bytes(forks[:data_size]),
                resource=bytes(forks[data_size:]),
            )
        )

    names = {entry.name for entry in entries}
    if names != EXPECTED_NAMES or len(entries) != len(EXPECTED_NAMES):
        raise ValueError(f"unexpected USB Overdrive payload: {sorted(names)!r}")
    return entries


def padded(data: bytes) -> bytes:
    return data + bytes((-len(data)) % 128)


def macbinary(entry: Entry) -> bytes:
    name = entry.name.encode("mac_roman")
    header = bytearray(128)
    header[1] = len(name)
    header[2 : 2 + len(name)] = name
    header[65:69] = entry.file_type
    header[69:73] = entry.creator
    header[73] = (entry.finder_flags >> 8) & 0xFF
    struct.pack_into(">I", header, 83, len(entry.data))
    struct.pack_into(">I", header, 87, len(entry.resource))
    struct.pack_into(">I", header, 91, entry.created)
    struct.pack_into(">I", header, 95, entry.modified)
    header[101] = entry.finder_flags & 0xFF
    header[122] = 129
    header[123] = 129
    struct.pack_into(">H", header, 124, crc16_xmodem(bytes(header[:124])))
    return bytes(header) + padded(entry.data) + padded(entry.resource)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("installer", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    args.output_directory.mkdir(parents=True, exist_ok=True)

    for entry in parse_installer(args.installer):
        output = args.output_directory / f"{entry.name}.bin"
        output.write_bytes(macbinary(entry))
        print(
            f"extracted {entry.name}: {len(entry.data)} data bytes, "
            f"{len(entry.resource)} resource bytes"
        )


if __name__ == "__main__":
    main()
