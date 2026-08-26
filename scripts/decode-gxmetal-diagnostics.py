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
CONTEXT_LIFECYCLE_FIELDS = (
    "draw_private_new_success_count",
    "draw_private_delete_count",
)
FOG_STATE_FIELDS = (
    "current_state_fog_color_a",
    "current_state_fog_color_r",
    "current_state_fog_color_g",
    "current_state_fog_color_b",
    "current_state_fog_start",
    "current_state_fog_end",
    "current_state_fog_density",
    "current_state_fog_max_depth",
)
ATI_PRIVATE_TRACE_FIELDS = (
    "ati_private_call_count",
    "ati_private_method_mask_low",
    "ati_private_method_mask_high",
    "ati_private_last_method",
    "ati_private_last_arg0",
    "ati_private_last_arg1",
    "ati_private_last_arg2",
    "ati_private_last_arg3",
    "ati_private_last_arg4",
    "ati_private_last_arg5",
    "ati_private_last_arg6",
    "ati_private_last_arg7",
)
ATI_PRIVATE_DRAW47_FIELDS = (
    "ati_private_draw47_call_count",
    "ati_private_draw47_arg0",
    "ati_private_draw47_arg1",
    "ati_private_draw47_arg2",
    "ati_private_draw47_arg3",
    "ati_private_draw47_arg4",
    "ati_private_draw47_arg5",
    "ati_private_draw47_arg6",
    "ati_private_draw47_arg7",
)
ATI_PRIVATE_DRAW47_VERTEX_FIELDS = (
    "ati_private_draw47_vertex_snapshot_valid",
    "ati_private_draw47_vertex_words",
)
ATI_PRIVATE_CLEAR_STATE_FIELDS = (
    "ati_private_state20_call_count",
    "ati_private_state20_arg0",
    "ati_private_state20_arg1",
    "ati_private_state20_arg2",
    "ati_private_state20_arg3",
    "ati_private_state20_arg4",
    "ati_private_state20_arg5",
    "ati_private_state20_arg6",
    "ati_private_state20_arg7",
    "ati_private_state20_snapshot_valid",
    "ati_private_state20_words",
    "ati_private_clear27_call_count",
    "ati_private_clear27_arg0",
    "ati_private_clear27_arg1",
    "ati_private_clear27_arg2",
    "ati_private_clear27_arg3",
    "ati_private_clear27_arg4",
    "ati_private_clear27_arg5",
    "ati_private_clear27_arg6",
    "ati_private_clear27_arg7",
    "ati_private_clear27_rect_snapshot_valid",
    "ati_private_clear27_rect_words",
)
ATI_PRIVATE_PIXEL21_FIELDS = (
    "ati_private_pixel21_call_count",
    "ati_private_pixel21_arg0",
    "ati_private_pixel21_arg1",
    "ati_private_pixel21_arg2",
    "ati_private_pixel21_arg3",
    "ati_private_pixel21_arg4",
    "ati_private_pixel21_arg5",
    "ati_private_pixel21_arg6",
    "ati_private_pixel21_arg7",
    "ati_private_pixel21_arg1_snapshot_valid",
    "ati_private_pixel21_arg1_words",
    "ati_private_pixel21_arg4_snapshot_valid",
    "ati_private_pixel21_arg4_words",
)
ATI_PRIVATE_STRIP_FIELDS = (
    "ati_private_draw49_call_count",
    "ati_private_draw49_last_vertex_count",
    "ati_private_draw49_max_vertex_count",
    "ati_private_draw50_call_count",
    "ati_private_draw50_last_vertex_count",
    "ati_private_draw50_max_vertex_count",
)
ATI_PRIVATE_PRIMITIVE_FIELDS = (
    "ati_private_draw49_last_primitive",
    "ati_private_draw49_primitive_mask",
    "ati_private_draw50_last_primitive",
    "ati_private_draw50_primitive_mask",
    "ati_private_draw51_call_count",
    "ati_private_draw51_last_vertex_count",
    "ati_private_draw51_max_vertex_count",
    "ati_private_draw51_last_primitive",
    "ati_private_draw51_primitive_mask",
    "ati_private_draw52_call_count",
    "ati_private_draw52_last_vertex_count",
    "ati_private_draw52_max_vertex_count",
    "ati_private_draw52_last_primitive",
    "ati_private_draw52_primitive_mask",
)
ATI_PRIVATE_FILL_FIELDS = (
    "ati_private_fill41_44_call_count",
    "ati_private_fill41_44_last_vertex_count",
    "ati_private_fill41_44_max_vertex_count",
    "ati_private_fill41_44_last_primitive",
    "ati_private_fill41_44_primitive_mask",
)
ATI_PRIVATE_GEOMETRY_FIELDS = (
    "ati_private_geometry_call_count",
    "ati_private_geometry_last_args",
)
ATI_PRIVATE_DRAW48_CAPTURE_FIELDS = (
    "ati_private_draw48_vertex_snapshot_valid_mask",
    "ati_private_draw48_vertex_words",
)
ATI_PRIVATE_FINISH_COUNTER_FIELDS = (
    "ati_private_draw60_zero_clip_marker_call_count",
    "ati_private_draw60_nonzero_clip_marker_call_count",
)
ATI_PRIVATE_FINISH_DETAIL_FIELDS = (
    "ati_private_draw60_clip_marker_or",
    "ati_private_draw60_clip_marker_low_value_count",
    "ati_private_draw60_clip_marker_high_value_call_count",
    "ati_private_draw60_vertex_count_buckets",
    "ati_private_draw60_nonzero_last_args",
    "ati_private_draw60_nonzero_pointer_snapshot_valid",
    "ati_private_draw60_nonzero_pointer_count",
    "ati_private_draw60_nonzero_vertex_snapshot_valid_mask",
    "ati_private_draw60_nonzero_vertex_pointers",
    "ati_private_draw60_nonzero_vertex_words",
    "ati_private_method_call_count",
    "ati_private_method28_29_last_args",
)
ATI_PRIVATE_TRANSITION_FIELDS = (
    "ati_private_frame_sequence",
    "ati_private_state20_dirty_mask_or",
    "ati_private_state20_word53_last",
    "ati_private_state20_word53_or",
    "ati_private_state20_word53_change_count",
    "ati_private_state20_word53_nonzero_call_count",
    "ati_private_state20_word53_first_nonzero_frame",
    "ati_private_context_resolve_count",
    "ati_private_context_fallback_count",
    "ati_private_context_last_renderer",
    "ati_private_context_last_draw_context",
    "ati_private_draw48_vertex_count_buckets",
    "ati_private_draw48_max_vertex_count",
    "ati_private_draw48_invalid_vertex_count_call_count",
    "ati_private_draw50_pointer_call_count",
    "ati_private_draw50_strip_call_count",
    "ati_private_geometry_triangle_attempt_count",
    "ati_private_geometry_triangle_queued_count",
    "ati_private_geometry_triangle_rejected_count",
    "ati_private_geometry_input_rejected_call_count",
    "ati_private_geometry_anomaly_count",
    "ati_private_geometry_anomaly_flags_or",
    "ati_private_geometry_first_anomaly_method",
    "ati_private_geometry_first_anomaly_frame",
    "ati_private_geometry_first_anomaly_flags",
    "ati_private_geometry_first_anomaly_vertex_addresses",
    "ati_private_geometry_first_anomaly_vertex_words",
)
ATI_PRIVATE_BURST_FIELDS = (
    "ati_private_geometry_current_frame_call_count",
    "ati_private_geometry_max_frame_call_count",
    "ati_private_geometry_max_frame_call_frame",
    "ati_private_geometry_first_burst_method",
    "ati_private_geometry_first_burst_frame",
    "ati_private_geometry_first_burst_call_count",
    "ati_private_geometry_first_burst_viewport_width",
    "ati_private_geometry_first_burst_viewport_height",
    "ati_private_geometry_first_burst_vertex_addresses",
    "ati_private_geometry_first_burst_vertex_words",
)
ATI_PRIVATE_FINISH_FIELDS = (
    ATI_PRIVATE_FINISH_COUNTER_FIELDS + ATI_PRIVATE_FINISH_DETAIL_FIELDS
    + ATI_PRIVATE_TRANSITION_FIELDS + ATI_PRIVATE_BURST_FIELDS
)
ATI_PRIVATE_VERTEX_CAPTURE_FIELDS = (
    "ati_private_draw50_vertex_snapshot_valid_mask",
    "ati_private_draw50_vertex_words",
    "ati_private_draw60_pointer_snapshot_valid",
    "ati_private_draw60_pointer_count",
    "ati_private_draw60_vertex_snapshot_valid_mask",
    "ati_private_draw60_vertex_pointers",
    "ati_private_draw60_vertex_words",
) + ATI_PRIVATE_DRAW48_CAPTURE_FIELDS + ATI_PRIVATE_FINISH_FIELDS
LEGACY_TRAILING_FIELDS = {
    0x0001000C: (CONTEXT_LIFECYCLE_FIELDS + FOG_STATE_FIELDS
                + ATI_PRIVATE_TRACE_FIELDS + ATI_PRIVATE_DRAW47_FIELDS
                + ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                + ATI_PRIVATE_CLEAR_STATE_FIELDS
                + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                + ATI_PRIVATE_GEOMETRY_FIELDS
                + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x0001000D: (FOG_STATE_FIELDS + ATI_PRIVATE_TRACE_FIELDS
                + ATI_PRIVATE_DRAW47_FIELDS
                + ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                + ATI_PRIVATE_CLEAR_STATE_FIELDS
                + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                + ATI_PRIVATE_GEOMETRY_FIELDS
                + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x0001000E: (ATI_PRIVATE_TRACE_FIELDS + ATI_PRIVATE_DRAW47_FIELDS
                + ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                + ATI_PRIVATE_CLEAR_STATE_FIELDS
                + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                + ATI_PRIVATE_GEOMETRY_FIELDS
                + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x0001000F: (ATI_PRIVATE_DRAW47_FIELDS
                + ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                + ATI_PRIVATE_CLEAR_STATE_FIELDS
                + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                + ATI_PRIVATE_GEOMETRY_FIELDS
                + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010010: (ATI_PRIVATE_DRAW47_VERTEX_FIELDS
                 + ATI_PRIVATE_CLEAR_STATE_FIELDS
                 + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                 + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                 + ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010011: (ATI_PRIVATE_CLEAR_STATE_FIELDS
                 + ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                 + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                 + ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010012: (ATI_PRIVATE_PIXEL21_FIELDS + ATI_PRIVATE_STRIP_FIELDS
                 + ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                 + ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010013: (ATI_PRIVATE_STRIP_FIELDS + ATI_PRIVATE_PRIMITIVE_FIELDS
                 + ATI_PRIVATE_FILL_FIELDS + ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010014: (ATI_PRIVATE_PRIMITIVE_FIELDS + ATI_PRIVATE_FILL_FIELDS
                 + ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010015: (ATI_PRIVATE_GEOMETRY_FIELDS
                 + ATI_PRIVATE_VERTEX_CAPTURE_FIELDS),
    0x00010016: ATI_PRIVATE_VERTEX_CAPTURE_FIELDS,
    0x00010017: (ATI_PRIVATE_DRAW48_CAPTURE_FIELDS
                 + ATI_PRIVATE_FINISH_FIELDS),
    0x00010018: ATI_PRIVATE_FINISH_FIELDS,
    0x00010019: (ATI_PRIVATE_FINISH_DETAIL_FIELDS
                 + ATI_PRIVATE_TRANSITION_FIELDS
                 + ATI_PRIVATE_BURST_FIELDS),
    0x0001001A: (ATI_PRIVATE_TRANSITION_FIELDS
                 + ATI_PRIVATE_BURST_FIELDS),
    0x0001001B: ATI_PRIVATE_BURST_FIELDS,
}


def parse_schema(header: Path):
    source = header.read_text(encoding="utf-8")
    macros = {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(GXMETAL_DIAGNOSTIC_[A-Z0-9_]+)\s+(\d+)u?\s*$",
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
    decode_fields = fields
    if len(data) != expected and len(data) >= 8:
        version = struct.unpack_from(">I", data, 4)[0]
        missing = LEGACY_TRAILING_FIELDS.get(version, ())
        trailing_names = [
            name for name, _, _ in fields[-len(missing):]
        ] if missing else []
        if trailing_names == list(missing) and missing:
            legacy_fields = fields[:-len(missing)]
            legacy_expected = sum(
                extent for _, _, extent in legacy_fields) * 4
            if len(data) == legacy_expected:
                decode_fields = legacy_fields
    decoded_expected = sum(extent for _, _, extent in decode_fields) * 4
    if len(data) != decoded_expected:
        raise ValueError(
            f"snapshot is {len(data)} bytes; header describes {expected} bytes"
        )

    result = {}
    offset = 0
    for name, signed, extent in decode_fields:
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
