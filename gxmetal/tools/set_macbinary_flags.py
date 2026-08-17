#!/usr/bin/env python3
"""Set or clear Finder flags in a MacBinary II header and repair its CRC."""

from __future__ import annotations

import argparse
import binascii
from pathlib import Path


def numeric(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0xFFFF:
        raise argparse.ArgumentTypeError("Finder flags must fit in 16 bits")
    return parsed


def update_flags(data: bytearray, set_flags: int, clear_flags: int) -> int:
    if len(data) < 128 or data[0] != 0 or data[1] == 0 or data[74] != 0:
        raise ValueError("not a MacBinary header")
    flags = (data[73] << 8) | data[101]
    flags = (flags | set_flags) & ~clear_flags
    data[73] = (flags >> 8) & 0xFF
    data[101] = flags & 0xFF
    crc = binascii.crc_hqx(bytes(data[:124]), 0)
    data[124:126] = crc.to_bytes(2, "big")
    return flags


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--set", type=numeric, default=0, dest="set_flags")
    parser.add_argument("--clear", type=numeric, default=0, dest="clear_flags")
    args = parser.parse_args()

    data = bytearray(args.path.read_bytes())
    try:
        flags = update_flags(data, args.set_flags, args.clear_flags)
    except ValueError as error:
        raise SystemExit(f"{args.path}: {error}") from error
    args.path.write_bytes(data)
    print(f"{args.path.name}: Finder flags 0x{flags:04X}")


if __name__ == "__main__":
    main()
