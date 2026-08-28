#!/usr/bin/env python3
"""Audit GXMetal sweep evidence against the full qualification contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


REVIEW_SCHEMA_VERSION = 2
TRUE_REVIEW_FIELDS = (
    "renderer_identified",
    "launch",
    "menus",
    "gameplay",
    "effects_heavy_scene",
    "visual_correctness",
    "input",
    "clean_exit",
    "second_launch",
    "context_teardown_observed",
    "fresh_context_observed",
    "stability",
    "source_provenance",
    "source_disk_unchanged",
    "gxmetal_profile_observed",
    "fault_free",
)


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def has_option(command: list[str], option: str, value: str) -> bool:
    return any(
        command[index] == option and command[index + 1] == value
        for index in range(len(command) - 1)
    )


def run_directory(session_dir: Path, result: dict[str, Any]) -> Path:
    local = session_dir / result["run_id"]
    if local.is_dir():
        return local
    recorded = Path(result.get("evidence", ""))
    return recorded if recorded.is_dir() else local


def backfill_log_facts(run_dir: Path, result: dict[str, Any]) -> dict[str, Any]:
    """Read old evidence logs when result.json predates automatic summaries."""
    facts = dict(result)
    required = (
        "gxmetal_profile_observed", "queue_faults", "transport_failures")
    if all(field in facts for field in required):
        return facts
    log_path = run_dir / "qemu.log"
    if not log_path.is_file():
        return facts
    profile_observed = False
    queue_faults = 0
    transport_failures = 0
    with log_path.open("r", encoding="utf-8", errors="replace") as log:
        for line in log:
            if "GXMetal profile:" in line:
                profile_observed = True
            if "GXMetal: queue fault" in line:
                queue_faults += 1
            if "transport connection failed" in line.lower():
                transport_failures += 1
    facts.setdefault("gxmetal_profile_observed", profile_observed)
    facts.setdefault("queue_faults", queue_faults)
    facts.setdefault("transport_failures", transport_failures)
    return facts


def audit_run(
    session_dir: Path,
    session: dict[str, Any],
    recorded_result: dict[str, Any],
) -> dict[str, Any]:
    run_id = recorded_result.get("run_id", "<missing-run-id>")
    problems: list[str] = []
    run_dir = run_directory(session_dir, recorded_result)
    result_path = run_dir / "result.json"
    review_path = run_dir / "review.json"
    command_path = run_dir / "qemu-command.json"

    if session.get("audio_backend") != "none":
        problems.append("session audio backend is not disabled")
    if session.get("base_unchanged") is not True:
        problems.append("source disk integrity is not proven")

    if not result_path.is_file():
        problems.append("result.json is missing")
        result = recorded_result
    else:
        result = read_json(result_path)
    result = backfill_log_facts(run_dir, result)
    if result.get("status") != "automation-complete":
        problems.append("automation did not complete")
    if result.get("qemu_exit_code") != 0:
        problems.append("QEMU did not exit cleanly")
    if result.get("audio_backend") != "none":
        problems.append("run audio backend is not disabled")
    if "gxmetal_profile_observed" not in result:
        problems.append("GXMetal profile evidence is unavailable")
    elif result["gxmetal_profile_observed"] is not True:
        problems.append("GXMetal profile output was not observed")
    if "direct_frames" not in result:
        problems.append("direct-presentation evidence is unavailable")
    elif result["direct_frames"] <= 0:
        problems.append("no direct GXMetal frames were presented")
    if "fallback_frames" not in result:
        problems.append("fallback-presentation evidence is unavailable")
    elif result["fallback_frames"] != 0:
        problems.append("fallback presentation was observed")
    if result.get("render_resets", 0) < 2:
        problems.append("two renderer resets/generations were not observed")
    context_create_generations = result.get("context_create_generations", [])
    if (not isinstance(context_create_generations, list) or
            len(set(context_create_generations)) < 2):
        problems.append(
            "fresh renderer contexts in two generations were not observed")
    if result.get("max_render_generation", 0) < 2:
        problems.append("a second render generation was not observed")
    if "queue_faults" not in result:
        problems.append("queue-fault evidence is unavailable")
    elif result["queue_faults"] != 0:
        problems.append("queue fault observed")
    if "transport_failures" not in result:
        problems.append("transport-failure evidence is unavailable")
    elif result["transport_failures"] != 0:
        problems.append("transport failure observed")

    if not command_path.is_file():
        problems.append("qemu-command.json is missing")
    else:
        command = read_json(command_path)
        if not isinstance(command, list) or not all(
                isinstance(item, str) for item in command):
            problems.append("QEMU command is malformed")
        else:
            if not has_option(command, "-audiodev", "none,id=snd0"):
                problems.append("QEMU command does not disable host audio")
            if not has_option(command, "-nic", "none"):
                problems.append("QEMU command does not disable networking")

    if not review_path.is_file():
        problems.append("review.json is missing")
        review: dict[str, Any] = {}
    else:
        review = read_json(review_path)
    if review.get("schema") != REVIEW_SCHEMA_VERSION:
        problems.append(f"review schema is not {REVIEW_SCHEMA_VERSION}")
    if review.get("review_status") != "qualified":
        problems.append("human review is not marked qualified")
    for field in TRUE_REVIEW_FIELDS:
        if review.get(field) is not True:
            problems.append(f"review field {field} is not true")
    if review.get("unexpected_fallback") is not False:
        problems.append("unexpected fallback has not been ruled out")
    if review.get("audio") != "disabled":
        problems.append("review does not record muted audio")
    if review.get("network") != "disabled":
        problems.append("review does not record disabled networking")

    return {
        "run_id": run_id,
        "game_id": recorded_result.get("game_id"),
        "evidence": str(run_dir),
        "qualified": not problems,
        "problems": problems,
    }


def audit_session(session_dir: Path) -> dict[str, Any]:
    session_dir = session_dir.resolve()
    session_path = session_dir / "session.json"
    if not session_path.is_file():
        raise ValueError(f"session.json does not exist in {session_dir}")
    session = read_json(session_path)
    recorded_results = session.get("results")
    if not isinstance(recorded_results, list) or not recorded_results:
        raise ValueError(f"session has no recorded results: {session_path}")
    runs = [audit_run(session_dir, session, result)
            for result in recorded_results]
    return {
        "session": str(session_dir),
        "qualified": all(run["qualified"] for run in runs),
        "runs": runs,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Audit GXMetal sweep evidence for full game qualification")
    parser.add_argument("evidence", nargs="+", type=Path,
                        help="one or more evidence session directories")
    parser.add_argument("--json", action="store_true",
                        help="emit the complete machine-readable report")
    args = parser.parse_args()

    reports: list[dict[str, Any]] = []
    try:
        reports = [audit_session(path) for path in args.evidence]
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))

    if args.json:
        print(json.dumps(reports, indent=2, sort_keys=True))
    else:
        for report in reports:
            for run in report["runs"]:
                state = "QUALIFIED" if run["qualified"] else "INCOMPLETE"
                print(f"[{state}] {run['run_id']}")
                for problem in run["problems"]:
                    print(f"  - {problem}")
        total = sum(len(report["runs"]) for report in reports)
        qualified = sum(
            1 for report in reports for run in report["runs"]
            if run["qualified"])
        print(f"Qualified runs: {qualified}/{total}")
    return 0 if all(report["qualified"] for report in reports) else 1


if __name__ == "__main__":
    sys.exit(main())
