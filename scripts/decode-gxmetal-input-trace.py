#!/usr/bin/env python3
"""Decode the native big-endian GXMetal Input Trace as JSON."""

import argparse
import json
import struct
import sys
from pathlib import Path


MAGIC = 0x47584954
VERSION = 0x00010000
EVENT_WORDS = 17
EVENT_CAPACITY = 32
HEADER_FIELDS = (
    "magic", "version", "snapshot_bytes", "event_capacity",
    "event_sequence", "cfm_initialize_count", "check_configuration_count",
    "find_count", "dispose_count", "driver_tickle_count",
    "device_tickle_count", "set_active_count", "set_active_true_count",
    "set_active_false_count", "stop_count", "reset_count",
    "owner_handoff_count", "dead_owner_forget_count",
    "live_owner_dispose_count", "stale_callback_count",
    "current_process_query_count", "current_process_success_count",
    "current_process_failure_count", "last_current_process_result",
    "last_current_process_high", "last_current_process_low",
    "owner_process_query_count", "owner_process_not_found_count",
    "owner_process_other_failure_count", "last_owner_process_result",
    "device_new_attempt_count", "device_new_success_count",
    "device_dispose_count", "element_new_attempt_count",
    "element_new_success_count", "element_dispose_count",
    "bridge_resolve_attempt_count", "bridge_resolve_success_count",
    "bridge_resolve_failure_count", "bridge_close_count",
    "host_mode_enable_attempt_count", "host_mode_disable_attempt_count",
    "host_mode_success_count", "host_mode_failure_count",
    "timer_start_count", "timer_stop_count", "timer_poll_count",
    "tickle_only_diagnostic", "timer_start_suppressed_count",
    "poll_count", "active_poll_count", "inactive_poll_count",
    "host_event_read_attempt_count", "host_event_read_success_count",
    "host_event_read_failure_count", "fallback_button_read_count",
    "delta_x_push_count", "delta_y_push_count", "button_1_push_count",
    "button_2_push_count", "button_3_push_count", "push_failure_count",
    "cfrag_context_id", "cfrag_closure_id", "cfrag_connection_id",
    "trace_load_count", "trace_load_valid_count",
    "trace_persist_attempt_count", "trace_persist_success_count",
    "trace_persist_failure_count",
)
EVENT_FIELDS = (
    "sequence", "kind", "ref_con", "argument", "result",
    "current_process_result", "current_process_high", "current_process_low",
    "owner_valid", "owner_process_result", "owner_process_high",
    "owner_process_low", "find_count", "set_active_count",
    "device_tickle_count", "poll_count", "push_count",
)
SIGNED_FIELDS = {
    "last_current_process_result", "last_owner_process_result", "result",
    "current_process_result", "owner_process_result",
}
EVENT_NAMES = {
    1: "cfm_initialize", 2: "check_configuration", 3: "find_enter",
    4: "find_exit", 5: "dispose_enter", 6: "dispose_exit",
    7: "set_active_enter", 8: "set_active_exit", 9: "stop",
    10: "device_tickle", 11: "driver_tickle", 12: "reset",
    13: "create", 14: "stale_callback",
}


def signed(value):
    return value - 0x100000000 if value & 0x80000000 else value


def decode(path):
    data = path.read_bytes()
    expected = 4 * (len(HEADER_FIELDS) + EVENT_WORDS * EVENT_CAPACITY)
    if len(data) != expected:
        raise ValueError(f"expected {expected} bytes, found {len(data)}")
    words = struct.unpack(f">{len(data) // 4}I", data)
    header = dict(zip(HEADER_FIELDS, words[:len(HEADER_FIELDS)]))
    if header["magic"] != MAGIC or header["version"] != VERSION:
        raise ValueError("invalid GXMetal Input Trace magic or version")
    if header["snapshot_bytes"] != expected:
        raise ValueError("snapshot size field does not match the trace")
    for field in SIGNED_FIELDS:
        if field in header:
            header[field] = signed(header[field])
    events = []
    offset = len(HEADER_FIELDS)
    for index in range(EVENT_CAPACITY):
        values = words[offset + index * EVENT_WORDS:
                       offset + (index + 1) * EVENT_WORDS]
        event = dict(zip(EVENT_FIELDS, values))
        if event["sequence"] == 0:
            continue
        for field in SIGNED_FIELDS:
            if field in event:
                event[field] = signed(event[field])
        event["name"] = EVENT_NAMES.get(event["kind"], "unknown")
        events.append(event)
    events.sort(key=lambda event: event["sequence"])
    header["events"] = events
    return header


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        json.dump(decode(args.trace), sys.stdout, indent=2, sort_keys=True)
        print()
    except (OSError, ValueError, struct.error) as error:
        sys.exit(f"{args.trace}: {error}")


if __name__ == "__main__":
    main()
