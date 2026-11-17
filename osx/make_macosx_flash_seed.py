#!/usr/bin/env python3
"""Create original Core99 flash NVRAM; no guest code or user data.

Two contiguous 8KiB banks, each with a 32-byte flash header and ordinary
CHRP common, MacOS75 and free partitions. Copy per Mac, never overwrite
an existing Mac's backing. Actual QEMU qtest checks byte-for-byte equality.
"""
import argparse
from pathlib import Path
import struct
import zlib


def header(signature, length, name):
    value = bytearray(struct.pack(">BBH12s", signature, 0, length // 16, name))
    total = sum(value)
    while total > 255:
        total = (total & 255) + (total >> 8)
    value[1] = total
    return value


def bank(generation):
    value = bytearray(8192)
    value[:16] = header(0x5a, 32, b"wwwwwwwwwwww")
    value[32:48] = header(0x70, 0xc10, b"common")
    variables = b"output-device=ttya\0\0"
    value[48:48 + len(variables)] = variables
    value[0xc30:0xc40] = header(0xa0, 0x510, b"APL,MacOS75")
    value[0x1140:0x1150] = header(0x7f, 0xec0, b"wwwwwwwwwwww")
    struct.pack_into(">I", value, 20, generation)
    struct.pack_into(">I", value, 16, zlib.adler32(value[20:], 1))
    return bytes(value)


def seed():
    return bank(1) + bank(2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(seed())


if __name__ == "__main__":
    main()
