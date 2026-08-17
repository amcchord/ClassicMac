#!/usr/bin/env python3
"""Convert MakePEF's main entry into a CFM shared-library init entry.

Retro68 MakePEF currently always writes the XCOFF entry descriptor to the
PEF loader header's mainSection/mainOffset fields. CFM import libraries use
initSection/initOffset instead. This narrowly patches those six loader fields
and validates the surrounding PEF structure before changing anything.
"""

import argparse
import struct
import sys


CONTAINER_HEADER_BYTES = 40
SECTION_HEADER_BYTES = 28
LOADER_SECTION_KIND = 4
NO_SECTION = -1


def read_u32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def read_s32(data, offset):
    return struct.unpack_from(">i", data, offset)[0]


def loader_offset(data):
    if len(data) < CONTAINER_HEADER_BYTES or data[:12] != b"Joy!peffpwpc":
        raise ValueError("not a PowerPC PEF container")
    section_count = struct.unpack_from(">H", data, 32)[0]
    table_end = CONTAINER_HEADER_BYTES + section_count * SECTION_HEADER_BYTES
    if section_count == 0 or table_end > len(data):
        raise ValueError("invalid PEF section table")

    for index in range(section_count):
        header = CONTAINER_HEADER_BYTES + index * SECTION_HEADER_BYTES
        if data[header + 24] == LOADER_SECTION_KIND:
            offset = read_u32(data, header + 20)
            length = read_u32(data, header + 16)
            if length < 56 or offset > len(data) - length:
                raise ValueError("invalid PEF loader section")
            return offset
    raise ValueError("PEF has no loader section")


def describe(data):
    offset = loader_offset(data)
    return {
        "offset": offset,
        "main_section": read_s32(data, offset),
        "main_offset": read_u32(data, offset + 4),
        "init_section": read_s32(data, offset + 8),
        "init_offset": read_u32(data, offset + 12),
        "term_section": read_s32(data, offset + 16),
        "term_offset": read_u32(data, offset + 20),
    }


def set_init(path):
    with open(path, "rb") as source:
        data = bytearray(source.read())
    info = describe(data)
    if info["main_section"] < 0 or info["main_offset"] == 0xFFFFFFFF:
        raise ValueError("PEF has no valid XCOFF entry descriptor")
    if info["init_section"] != NO_SECTION:
        raise ValueError("PEF already has an initialization entry")

    offset = info["offset"]
    struct.pack_into(">iI", data, offset, NO_SECTION, 0)
    struct.pack_into(">iI", data, offset + 8,
                     info["main_section"], info["main_offset"])
    with open(path, "wb") as destination:
        destination.write(data)


def verify(path):
    with open(path, "rb") as source:
        info = describe(source.read())
    if info["main_section"] != NO_SECTION or info["main_offset"] != 0:
        raise ValueError("PEF still declares an application main entry")
    if info["init_section"] < 0 or info["init_offset"] == 0xFFFFFFFF:
        raise ValueError("PEF has no valid CFM initialization entry")
    print("GXMetal PEF: init section %d, descriptor 0x%08x" %
          (info["init_section"], info["init_offset"]))


def main():
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--set-init", action="store_true")
    action.add_argument("--verify", action="store_true")
    parser.add_argument("path")
    args = parser.parse_args()

    try:
        if args.set_init:
            set_init(args.path)
        else:
            verify(args.path)
    except (OSError, ValueError, struct.error) as error:
        sys.exit("%s: %s" % (args.path, error))


if __name__ == "__main__":
    main()
