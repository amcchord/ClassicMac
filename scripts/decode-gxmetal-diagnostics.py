#!/usr/bin/env python3
"""Decode a persisted GXMetal Driver Trace snapshot as JSON.

The guest writes GXMetalDiagnosticSnapshot in native PowerPC byte order.  This
tool derives the field layout from the authoritative C header so a diagnostic
format extension cannot silently shift every later counter in host analysis.
"""

import argparse
import json
import re
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HEADER = ROOT / "gxmetal/guest/include/GXMetalDiagnostics.h"


def parse_schema(header: Path):
    source = header.read_text(encoding="utf-8")
    macros = {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(GXMETAL_DIAGNOSTIC_[A-Z_]+)\s+(\d+)u?\s*$",
            source,
            re.MULTILINE,
        )
    }
    match = re.search(
        r"typedef\s+struct\s+GXMetalDiagnosticSnapshot\s*\{(.*?)\}"
        r"\s*GXMetalDiagnosticSnapshot\s*;",
        source,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"GXMetalDiagnosticSnapshot not found in {header}")

    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    fields = []
    for declaration in body.split(";"):
        declaration = " ".join(declaration.split())
        if not declaration:
            continue
        field = re.fullmatch(
            r"(u?int32_t)\s+([A-Za-z_][A-Za-z0-9_]*)"
            r"(?:\[\s*([A-Za-z0-9_]+)\s*\])?",
            declaration,
        )
        if not field:
            raise ValueError(f"unsupported diagnostic field: {declaration}")
        field_type, name, extent_token = field.groups()
        if extent_token is None:
            extent = 1
        elif extent_token.isdigit():
            extent = int(extent_token)
        elif extent_token in macros:
            extent = macros[extent_token]
        else:
            raise ValueError(f"unknown array extent {extent_token} for {name}")
        fields.append((name, field_type == "int32_t", extent))
    return fields


def decode_snapshot(data: bytes, fields):
    expected = sum(extent for _, _, extent in fields) * 4
    if len(data) != expected:
        raise ValueError(
            f"snapshot is {len(data)} bytes; header describes {expected} bytes"
        )

    result = {}
    offset = 0
    for name, signed, extent in fields:
        values = []
        format_code = ">i" if signed else ">I"
        for _ in range(extent):
            values.append(struct.unpack_from(format_code, data, offset)[0])
            offset += 4
        result[name] = values[0] if extent == 1 else values
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Decode a big-endian GXMetal Driver Trace snapshot"
    )
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    args = parser.parse_args()

    fields = parse_schema(args.header.resolve())
    result = decode_snapshot(args.snapshot.resolve().read_bytes(), fields)
    json.dump(result, fp=sys.stdout, indent=2, sort_keys=True)
    print()


if __name__ == "__main__":
    main()
