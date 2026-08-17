#!/usr/bin/env python3
"""Verify the Finder metadata embedded in a MacBinary II artifact."""

import argparse
import binascii
from pathlib import Path


def parse_fourcc(value: str) -> bytes:
    encoded = value.encode("mac_roman")
    if len(encoded) != 4:
        raise argparse.ArgumentTypeError("a Finder code must contain four bytes")
    return encoded


def parse_flags(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0xFFFF:
        raise argparse.ArgumentTypeError("Finder flags must fit in 16 bits")
    return parsed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--type", required=True, type=parse_fourcc,
                        dest="file_type")
    parser.add_argument("--creator", required=True, type=parse_fourcc)
    parser.add_argument("--require-flags", type=parse_flags, default=0)
    parser.add_argument("--forbid-flags", type=parse_flags, default=0)
    args = parser.parse_args()

    data = args.path.read_bytes()
    if len(data) < 128 or data[0] != 0 or data[1] == 0 or data[74] != 0:
        raise SystemExit(f"{args.path}: not a MacBinary header")
    expected_crc = int.from_bytes(data[124:126], "big")
    actual_crc = binascii.crc_hqx(data[:124], 0)
    if actual_crc != expected_crc:
        raise SystemExit(
            f"{args.path}: MacBinary CRC mismatch "
            f"(expected 0x{expected_crc:04X}, found 0x{actual_crc:04X})"
        )
    actual_type = data[65:69]
    actual_creator = data[69:73]
    actual_flags = (data[73] << 8) | data[101]
    if actual_type != args.file_type or actual_creator != args.creator:
        raise SystemExit(
            f"{args.path}: expected {args.file_type!r}/{args.creator!r}, "
            f"found {actual_type!r}/{actual_creator!r}"
        )
    if (actual_flags & args.require_flags) != args.require_flags:
        raise SystemExit(
            f"{args.path}: required Finder flags 0x{args.require_flags:04X}, "
            f"found 0x{actual_flags:04X}"
        )
    if actual_flags & args.forbid_flags:
        raise SystemExit(
            f"{args.path}: forbidden Finder flags 0x{args.forbid_flags:04X}, "
            f"found 0x{actual_flags:04X}"
        )
    print(
        f"{args.path.name}: Finder type {actual_type.decode('mac_roman')}, "
        f"creator {actual_creator.decode('mac_roman')}, "
        f"flags 0x{actual_flags:04X}"
    )


if __name__ == "__main__":
    main()
