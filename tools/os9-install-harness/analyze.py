#!/usr/bin/env python3
"""Validate invariants in an instrumented MacIO IDE trace."""

import argparse
import re
import sys


FIELDS = re.compile(r"([a-zA-Z_]+)=([^ ]+)")
MACIO = re.compile(r"macio-ide ([^ ]+)")


def fields(line):
    return dict(FIELDS.findall(line))


def controller(line):
    match = MACIO.search(line)
    return match.group(1) if match else "unknown"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--require-delay", action="store_true")
    args = parser.parse_args()

    submits = 0
    completions = 0
    delayed = 0
    fired = 0
    wrong_callback = 0
    state_mismatch = 0
    overlength = 0
    pending = {}

    with open(args.trace, errors="replace") as trace:
        for number, line in enumerate(trace, 1):
            if line.startswith("pmac_ide_transfer_submit"):
                data = fields(line)
                key = controller(line)
                submits += 1
                if int(data["len"]) > int(data["buffer"]):
                    overlength += 1
                if key in pending:
                    state_mismatch += 1
                pending[key] = (number, data)
            elif line.startswith("pmac_ide_transfer_cb"):
                data = fields(line)
                key = controller(line)
                if (data.get("kind") == "0" and
                        data.get("atapi_callback") == "1"):
                    wrong_callback += 1
                if data.get("residual") == "0" and key in pending:
                    _, submitted = pending.pop(key)
                    completions += 1
                    for key in ("unit", "kind", "dma_cmd"):
                        if data.get(key) != submitted.get(key):
                            state_mismatch += 1
                            break
                    if (data.get("atapi_callback") !=
                            submitted.get("atapi_submit")):
                        state_mismatch += 1
            elif line.startswith("pmac_ide_completion_scheduled"):
                delayed += 1
            elif line.startswith("pmac_ide_completion "):
                fired += 1

    failures = wrong_callback + state_mismatch + overlength
    failures += len(pending)
    if delayed != fired:
        failures += 1
    if args.require_delay and delayed == 0:
        failures += 1

    print(f"submits={submits}")
    print(f"aio_completions={completions}")
    print(f"delayed_completions={delayed}")
    print(f"fired_completions={fired}")
    print(f"wrong_callback={wrong_callback}")
    print(f"state_mismatch={state_mismatch}")
    print(f"overlength_submit={overlength}")
    print(f"pending_submit={len(pending)}")
    return int(failures != 0)


if __name__ == "__main__":
    sys.exit(main())
