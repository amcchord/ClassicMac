#!/usr/bin/env python3
"""Verify the Finder metadata embedded in a MacBinary II artifact."""

import argparse
from pathlib import Path


def parse_fourcc(value: str) -> bytes:
    encoded = value.encode("mac_roman")
    if len(encoded) != 4:
        raise argparse.ArgumentTypeError("a Finder code must contain four bytes")
    return encoded


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--type", required=True, type=parse_fourcc,
                        dest="file_type")
    parser.add_argument("--creator", required=True, type=parse_fourcc)
    args = parser.parse_args()

    data = args.path.read_bytes()
    if len(data) < 128 or data[0] != 0 or data[1] == 0 or data[74] != 0:
        raise SystemExit(f"{args.path}: not a MacBinary header")
    actual_type = data[65:69]
    actual_creator = data[69:73]
    if actual_type != args.file_type or actual_creator != args.creator:
        raise SystemExit(
            f"{args.path}: expected {args.file_type!r}/{args.creator!r}, "
            f"found {actual_type!r}/{actual_creator!r}"
        )
    print(
        f"{args.path.name}: Finder type {actual_type.decode('mac_roman')}, "
        f"creator {actual_creator.decode('mac_roman')}"
    )


if __name__ == "__main__":
    main()
