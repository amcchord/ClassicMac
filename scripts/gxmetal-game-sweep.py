#!/usr/bin/env python3
"""Run isolated, evidence-producing GXMetal game sessions in parallel.

Every QEMU process receives its own writable clone of a base Mac OS disk. The
base image is hashed before and after the sweep and is never passed to QEMU.
Game interaction is described by a JSON manifest and sent through the local
Unix-socket VNC client in scripts/gxmetal-vnc.py.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from types import ModuleType
from typing import Any


SCHEMA_VERSION = 1
VALID_MODES = ("gxmetal", "software")
ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
STEP_ACTIONS = (
    "wait", "wait_for_frame_change", "click", "double_click", "key",
    "chord", "text", "screenshot", "note",
)
# Keep these names synchronized with scripts/gxmetal-vnc.py. Single printable
# characters are accepted independently for ordinary text keys.
VALID_NAMED_KEYS = frozenset({
    "BackSpace", "Tab", "Return", "Escape", "Home", "Left", "Up",
    "Right", "Down", "PageUp", "PageDown", "End", "Insert", "Delete",
    "KP_Enter", "Shift_L", "Control_L", "Meta_L", "Alt_L", "Super_L",
    "Space",
})


@dataclass(frozen=True)
class RunSpec:
    game_id: str
    name: str
    mode: str
    cdrom: Path | None
    source_url: str | None
    source_sha256: str | None
    boot_wait_seconds: float
    observation_seconds: float
    capture_interval_seconds: float
    resolution: str
    steps: tuple[dict[str, Any], ...]

    @property
    def run_id(self) -> str:
        return f"{self.game_id}__{self.mode}"


class EventLog:
    def __init__(self, path: Path, started: float):
        self.output = path.open("w", encoding="utf-8")
        self.started = started

    def write(self, kind: str, **details: Any) -> None:
        event = {
            "elapsed_seconds": round(time.monotonic() - self.started, 3),
            "event": kind,
            **details,
        }
        self.output.write(json.dumps(event, sort_keys=True) + "\n")
        self.output.flush()

    def close(self) -> None:
        self.output.close()


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path, include_hash: bool = True) -> dict[str, Any]:
    stat = path.stat()
    record: dict[str, Any] = {
        "path": str(path),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }
    if include_hash:
        record["sha256"] = sha256_file(path)
    return record


def qemu_guest_name(name: str, mode: str) -> str:
    """Return a display label that QEMU's comma-delimited -name accepts."""
    safe_name = re.sub(r"[\s,]+", " ", name).strip()
    return f"GXMetal sweep: {safe_name} ({mode})"


def load_vnc_module(root: Path) -> ModuleType:
    path = root / "scripts/gxmetal-vnc.py"
    spec = importlib.util.spec_from_file_location("gxmetal_vnc", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load VNC helper: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def number(value: Any, field: str, *, positive: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number")
    result = float(value)
    if result < 0 or (positive and result <= 0):
        qualifier = "positive" if positive else "nonnegative"
        raise ValueError(f"{field} must be {qualifier}")
    return result


def validate_step(step: Any, field: str) -> dict[str, Any]:
    if not isinstance(step, dict):
        raise ValueError(f"{field} must be an object")
    actions = [key for key in STEP_ACTIONS if key in step]
    if len(actions) != 1:
        raise ValueError(f"{field} must contain exactly one VNC action")
    action = actions[0]
    value = step[action]
    if action == "wait":
        number(value, f"{field}.wait")
    elif action == "wait_for_frame_change":
        if not isinstance(value, dict):
            raise ValueError(
                f"{field}.wait_for_frame_change must be an object")
        unknown_wait_fields = set(value) - {
            "timeout_seconds", "poll_interval_seconds",
            "minimum_changed_fraction", "channel_tolerance",
        }
        if unknown_wait_fields:
            raise ValueError(
                f"{field}.wait_for_frame_change has unknown fields: "
                f"{sorted(unknown_wait_fields)}")
        timeout = number(value.get("timeout_seconds"),
                         f"{field}.wait_for_frame_change.timeout_seconds",
                         positive=True)
        poll_interval = number(
            value.get("poll_interval_seconds", 1),
            f"{field}.wait_for_frame_change.poll_interval_seconds",
            positive=True)
        minimum_fraction = number(
            value.get("minimum_changed_fraction", 0.02),
            f"{field}.wait_for_frame_change.minimum_changed_fraction",
            positive=True)
        tolerance = value.get("channel_tolerance", 8)
        if (isinstance(tolerance, bool) or not isinstance(tolerance, int) or
                tolerance < 0 or tolerance > 255):
            raise ValueError(
                f"{field}.wait_for_frame_change.channel_tolerance must be "
                "an integer from 0 through 255")
        if timeout < poll_interval:
            raise ValueError(
                f"{field}.wait_for_frame_change timeout must be at least "
                "the poll interval")
        if minimum_fraction > 1:
            raise ValueError(
                f"{field}.wait_for_frame_change.minimum_changed_fraction "
                "must not exceed 1")
    elif action in ("click", "double_click"):
        if (not isinstance(value, list) or len(value) != 2 or
                any(isinstance(item, bool) or not isinstance(item, int)
                    for item in value)):
            raise ValueError(f"{field}.click must be [x, y] integers")
    elif not isinstance(value, str) or not value:
        raise ValueError(f"{field}.{action} must be a nonempty string")
    elif action == "key" and not (
            len(value) == 1 or value in VALID_NAMED_KEYS):
        raise ValueError(f"{field}.key has unknown VNC key: {value}")
    elif action == "chord":
        names = value.split("+")
        if (len(names) < 2 or any(
                not (len(name) == 1 or name in VALID_NAMED_KEYS)
                for name in names)):
            raise ValueError(f"{field}.chord has unknown or missing VNC key")
    unknown = set(step) - {action, "delay_after", "hold_ms", "capture_after"}
    if unknown:
        raise ValueError(f"{field} has unknown fields: {sorted(unknown)}")
    if "delay_after" in step:
        number(step["delay_after"], f"{field}.delay_after")
    if "hold_ms" in step:
        number(step["hold_ms"], f"{field}.hold_ms", positive=True)
    if "capture_after" in step and not isinstance(step["capture_after"], bool):
        raise ValueError(f"{field}.capture_after must be a boolean")
    return dict(step)


def load_manifest(path: Path, selected_modes: tuple[str, ...]) -> list[RunSpec]:
    with path.open(encoding="utf-8") as source:
        manifest = json.load(source)
    if not isinstance(manifest, dict):
        raise ValueError("manifest must be a JSON object")
    if manifest.get("schema") != SCHEMA_VERSION:
        raise ValueError(f"manifest schema must be {SCHEMA_VERSION}")
    defaults = manifest.get("defaults", {})
    games = manifest.get("games")
    if not isinstance(defaults, dict) or not isinstance(games, list) or not games:
        raise ValueError("manifest requires a nonempty games array")

    boot_default = number(defaults.get("boot_wait_seconds", 75),
                          "defaults.boot_wait_seconds")
    observation_default = number(defaults.get("observation_seconds", 60),
                                  "defaults.observation_seconds")
    capture_default = number(defaults.get("capture_interval_seconds", 15),
                              "defaults.capture_interval_seconds", positive=True)
    resolution_default = defaults.get("resolution", "1024x768x15")
    if (not isinstance(resolution_default, str) or
            re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*x(8|15|16|24|32)",
                         resolution_default) is None):
        raise ValueError("defaults.resolution must look like 1024x768x15")

    seen: set[str] = set()
    specs: list[RunSpec] = []
    for index, game in enumerate(games):
        field = f"games[{index}]"
        if not isinstance(game, dict):
            raise ValueError(f"{field} must be an object")
        if "enabled" in game and not isinstance(game["enabled"], bool):
            raise ValueError(f"{field}.enabled must be a boolean")
        if game.get("enabled", True) is False:
            continue
        game_id = game.get("id")
        name = game.get("name")
        if not isinstance(game_id, str) or ID_PATTERN.fullmatch(game_id) is None:
            raise ValueError(f"{field}.id must match {ID_PATTERN.pattern}")
        if game_id in seen:
            raise ValueError(f"duplicate game id: {game_id}")
        seen.add(game_id)
        if not isinstance(name, str) or not name:
            raise ValueError(f"{field}.name must be a nonempty string")

        game_modes = game.get("modes", list(selected_modes))
        if not isinstance(game_modes, list) or not game_modes:
            raise ValueError(f"{field}.modes must be a nonempty array")
        if any(mode not in VALID_MODES for mode in game_modes):
            raise ValueError(f"{field}.modes accepts only {VALID_MODES}")
        if len(set(game_modes)) != len(game_modes):
            raise ValueError(f"{field}.modes contains duplicates")
        modes = [mode for mode in game_modes if mode in selected_modes]
        if not modes:
            continue

        cdrom_value = game.get("cdrom")
        cdrom: Path | None = None
        if cdrom_value is not None:
            if not isinstance(cdrom_value, str) or not cdrom_value:
                raise ValueError(f"{field}.cdrom must be a path string")
            cdrom = Path(cdrom_value).expanduser()
            if not cdrom.is_absolute():
                cdrom = path.parent / cdrom
            cdrom = cdrom.resolve()
            if not cdrom.is_file():
                raise ValueError(f"{field}.cdrom does not exist: {cdrom}")

        source_url = game.get("source_url")
        source_sha256 = game.get("source_sha256")
        if source_url is not None and not isinstance(source_url, str):
            raise ValueError(f"{field}.source_url must be a string")
        if source_sha256 is not None:
            if (not isinstance(source_sha256, str) or
                    re.fullmatch(r"[0-9a-fA-F]{64}", source_sha256) is None):
                raise ValueError(f"{field}.source_sha256 must be a SHA-256 hex digest")
            source_sha256 = source_sha256.lower()
            if cdrom is None:
                raise ValueError(f"{field}.source_sha256 requires cdrom")

        step_values = game.get("steps", [])
        if not isinstance(step_values, list):
            raise ValueError(f"{field}.steps must be an array")
        steps = tuple(validate_step(step, f"{field}.steps[{step_index}]")
                      for step_index, step in enumerate(step_values))
        boot_wait = number(game.get("boot_wait_seconds", boot_default),
                           f"{field}.boot_wait_seconds")
        observation = number(game.get("observation_seconds", observation_default),
                             f"{field}.observation_seconds")
        capture_interval = number(
            game.get("capture_interval_seconds", capture_default),
            f"{field}.capture_interval_seconds", positive=True)
        resolution = game.get("resolution", resolution_default)
        if (not isinstance(resolution, str) or
                re.fullmatch(r"[1-9][0-9]*x[1-9][0-9]*x(8|15|16|24|32)",
                             resolution) is None):
            raise ValueError(f"{field}.resolution must look like 1024x768x15")

        for mode in modes:
            specs.append(RunSpec(
                game_id=game_id,
                name=name,
                mode=mode,
                cdrom=cdrom,
                source_url=source_url,
                source_sha256=source_sha256,
                boot_wait_seconds=boot_wait,
                observation_seconds=observation,
                capture_interval_seconds=capture_interval,
                resolution=resolution,
                steps=steps,
            ))
    if not specs:
        raise ValueError("no enabled games match the selected modes")
    return specs


def clone_disk(source: Path, destination: Path) -> str:
    destination.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        ["cp", "-c", str(source), str(destination)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    method = "clonefile"
    if result.returncode != 0:
        destination.unlink(missing_ok=True)
        shutil.copy2(source, destination)
        method = "full-copy"
    destination.chmod(destination.stat().st_mode | 0o200)
    return method


def qemu_command(
    root: Path,
    qemu: Path,
    base_clone: Path,
    run_dir: Path,
    socket_dir: Path,
    spec: RunSpec,
    loader: Path,
    firmware: Path,
    tools_cd: Path | None,
    ram_mb: int,
    cpu: str,
    accel: str,
) -> list[str]:
    paths = [base_clone, loader, firmware]
    if spec.cdrom is not None:
        paths.append(spec.cdrom)
    if tools_cd is not None:
        paths.append(tools_cd)
    if any("," in str(path) for path in paths):
        raise ValueError("QEMU -drive paths containing commas are not supported")
    command = [
        str(qemu),
        "-accel", accel,
        "-M", "mac99,via=cuda,audiodev=snd0",
        "-cpu", cpu,
        "-m", str(ram_mb),
        "-L", str(firmware),
        "-display", "none",
        "-vnc", f"unix:{socket_dir / 'vnc.sock'}",
        "-vga", "std",
        "-global", "VGA.host-resize=on",
        "-global", "VGA.vgamem_mb=64",
        "-global", "VGA.packed-lowbpp=on",
        "-global", "VGA.untracked-vram=on",
        "-global", "VGA.hardware-cursor=on",
        "-global", f"VGA.gxmetal={'on' if spec.mode == 'gxmetal' else 'off'}",
        "-global", "macio-ide.dma-completion-delay-ns=1000000",
        "-prom-env", "output-device=ttya",
        "-g", spec.resolution,
        "-name", qemu_guest_name(spec.name, spec.mode),
        "-device", f"loader,addr=0x4000000,file={loader}",
        "-device", "virtio-tablet-pci",
        "-prom-env", "boot-command=init-program go",
        "-audiodev", "none,id=snd0",
        "-serial", f"file:{run_dir / 'serial.log'}",
        "-monitor", f"unix:{socket_dir / 'monitor.sock'},server=on,wait=off",
        "-action", "reboot=shutdown",
        "-drive", f"file={base_clone},format=raw,media=disk,index=0",
        "-nic", "none",
    ]
    if spec.cdrom is not None:
        command += [
            "-drive",
            f"file={spec.cdrom},format=raw,if=ide,index=2,media=cdrom,readonly=on",
        ]
    if tools_cd is not None:
        command += [
            "-drive",
            f"file={tools_cd},format=raw,if=ide,index=3,media=cdrom,readonly=on",
        ]
    return command


def wait_for_socket(path: Path, process: subprocess.Popen[bytes], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited while waiting for {path.name} ({process.returncode})")
        if path.exists():
            return
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {path}")


def connect_vnc(module: ModuleType, path: Path, process: subprocess.Popen[bytes],
                timeout: float) -> Any:
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited before VNC connected ({process.returncode})")
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.settimeout(10)
        try:
            connection.connect(str(path))
            client = module.RFBClient(connection)
            client.connect()
            return client
        except (OSError, RuntimeError) as error:
            last_error = error
            connection.close()
            time.sleep(0.1)
    raise RuntimeError(f"timed out connecting to VNC: {last_error}")


def quit_qemu(path: Path) -> None:
    if not path.exists():
        return
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
        monitor.settimeout(2)
        monitor.connect(str(path))
        response = bytearray()
        while not response.endswith(b"(qemu) "):
            chunk = monitor.recv(4096)
            if not chunk:
                return
            response.extend(chunk)
        monitor.sendall(b"quit\n")


def safe_label(value: str) -> str:
    label = re.sub(r"[^a-zA-Z0-9._-]+", "-", value).strip("-.")
    return label[:80] or "frame"


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def changed_pixel_fraction(previous: bytes, current: bytes,
                           channel_tolerance: int) -> float:
    """Return the fraction of RGB pixels with a material channel change."""
    if len(previous) != len(current) or len(current) % 3 != 0:
        raise ValueError("RGB frames must have the same whole-pixel length")
    pixel_count = len(current) // 3
    if pixel_count == 0:
        return 0.0
    changed = 0
    for offset in range(0, len(current), 3):
        if any(abs(current[offset + channel] - previous[offset + channel]) >
               channel_tolerance for channel in range(3)):
            changed += 1
    return changed / pixel_count


def captured_command(command: list[str], cwd: Path) -> dict[str, Any]:
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            check=False)
    return {
        "command": command,
        "exit_code": result.returncode,
        "output": result.stdout.strip(),
    }


def execute_run(
    root: Path,
    source_disk: Path,
    qemu: Path,
    loader: Path,
    firmware: Path,
    tools_cd: Path | None,
    output: Path,
    spec: RunSpec,
    vnc_module: ModuleType,
    args: argparse.Namespace,
) -> dict[str, Any]:
    run_dir = output / spec.run_id
    run_dir.mkdir(parents=True, exist_ok=False)
    screenshots = run_dir / "screenshots"
    screenshots.mkdir()
    clone = run_dir / "disk.img"
    socket_context = tempfile.TemporaryDirectory(prefix="gxmetal-sockets-")
    socket_dir = Path(socket_context.name)
    monitor_socket = socket_dir / "monitor.sock"
    vnc_socket = socket_dir / "vnc.sock"
    started = time.monotonic()
    wall_started = utc_now()
    events = EventLog(run_dir / "events.jsonl", started)
    process: subprocess.Popen[bytes] | None = None
    qemu_log = None
    client = None
    status = "failed"
    error_text: str | None = None
    capture_count = 0
    clone_method: str | None = None

    def capture_frame(label: str, rgb: bytes, width: int, height: int) -> None:
        nonlocal capture_count
        capture_count += 1
        elapsed = time.monotonic() - started
        filename = f"{capture_count:04d}-{elapsed:07.2f}s-{safe_label(label)}.png"
        path = screenshots / filename
        vnc_module.write_png(path, width, height, rgb)
        events.write("screenshot", file=str(path.relative_to(run_dir)),
                     width=width, height=height)

    def capture(label: str) -> None:
        if client is None:
            return
        rgb = client.capture()
        capture_frame(label, rgb, client.width, client.height)

    def wait_and_capture(seconds: float, label: str) -> None:
        deadline = time.monotonic() + seconds
        next_capture = min(deadline, time.monotonic() + spec.capture_interval_seconds)
        while time.monotonic() < deadline:
            if process is not None and process.poll() is not None:
                raise RuntimeError(f"QEMU exited during {label} ({process.returncode})")
            now = time.monotonic()
            if now >= next_capture:
                capture(label)
                next_capture += spec.capture_interval_seconds
                continue
            time.sleep(min(0.2, deadline - now, next_capture - now))

    def wait_for_frame_change(settings: dict[str, Any], label: str) -> None:
        if client is None:
            return
        timeout = float(settings["timeout_seconds"])
        poll_interval = float(settings.get("poll_interval_seconds", 1))
        minimum_fraction = float(
            settings.get("minimum_changed_fraction", 0.02))
        tolerance = int(settings.get("channel_tolerance", 8))
        baseline = client.capture()
        baseline_width = client.width
        baseline_height = client.height
        capture_frame(f"{label}-baseline", baseline,
                      baseline_width, baseline_height)
        deadline = time.monotonic() + timeout
        next_evidence = min(
            deadline, time.monotonic() + spec.capture_interval_seconds)
        while time.monotonic() < deadline:
            if process is not None and process.poll() is not None:
                raise RuntimeError(
                    f"QEMU exited during {label} ({process.returncode})")
            time.sleep(min(poll_interval, max(0, deadline - time.monotonic())))
            current = client.capture()
            width = client.width
            height = client.height
            if width != baseline_width or height != baseline_height:
                fraction = 1.0
            else:
                fraction = changed_pixel_fraction(
                    baseline, current, tolerance)
            if fraction >= minimum_fraction:
                capture_frame(f"{label}-detected", current, width, height)
                events.write(
                    "frame_change_detected", label=label,
                    changed_fraction=round(fraction, 6),
                    minimum_changed_fraction=minimum_fraction,
                    channel_tolerance=tolerance,
                    baseline_width=baseline_width,
                    baseline_height=baseline_height,
                    width=width, height=height)
                return
            if time.monotonic() >= next_evidence:
                capture_frame(label, current, width, height)
                next_evidence += spec.capture_interval_seconds
        events.write(
            "frame_change_timeout", label=label,
            minimum_changed_fraction=minimum_fraction,
            channel_tolerance=tolerance)
        raise RuntimeError(
            f"timed out waiting {timeout:g}s for a material frame change")

    try:
        events.write("clone_started", source=str(source_disk))
        clone_method = clone_disk(source_disk, clone)
        events.write("clone_completed", method=clone_method, size=clone.stat().st_size)
        command = qemu_command(root, qemu, clone, run_dir, socket_dir, spec,
                               loader, firmware, tools_cd, args.ram_mb,
                               args.cpu, args.accel)
        write_json(run_dir / "qemu-command.json", command)
        write_json(run_dir / "run.json", {
            "game_id": spec.game_id,
            "name": spec.name,
            "mode": spec.mode,
            "source_url": spec.source_url,
            "source_sha256": spec.source_sha256,
            "cdrom": str(spec.cdrom) if spec.cdrom else None,
            "started_at": wall_started,
            "resolution": spec.resolution,
            "steps": spec.steps,
        })
        qemu_log = (run_dir / "qemu.log").open("wb")
        environment = os.environ.copy()
        if spec.mode == "gxmetal":
            environment["GXMETAL_PROFILE"] = "1"
        events.write("qemu_started")
        process = subprocess.Popen(command, cwd=root, env=environment,
                                   stdout=qemu_log, stderr=subprocess.STDOUT)
        wait_for_socket(monitor_socket, process, args.start_timeout)
        wait_for_socket(vnc_socket, process, args.start_timeout)
        client = connect_vnc(vnc_module, vnc_socket, process,
                             args.start_timeout)
        events.write("vnc_connected", width=client.width, height=client.height)
        capture("boot-initial")
        wait_and_capture(spec.boot_wait_seconds, "boot")
        capture("boot-complete")

        for index, step in enumerate(spec.steps):
            action = next(key for key in STEP_ACTIONS if key in step)
            value = step[action]
            events.write("step", index=index, action=action, value=value)
            if action == "wait":
                wait_and_capture(float(value), f"step-{index:02d}-wait")
            elif action == "wait_for_frame_change":
                wait_for_frame_change(
                    value, f"step-{index:02d}-frame-change")
            elif action == "click":
                client.click(value[0], value[1])
            elif action == "double_click":
                client.double_click(value[0], value[1])
            elif action == "key":
                client.key(value, float(step.get("hold_ms", 150)) / 1000.0)
            elif action == "chord":
                client.chord(value, float(step.get("hold_ms", 150)) / 1000.0)
            elif action == "text":
                hold = float(step.get("hold_ms", 50)) / 1000.0
                for character in value:
                    client.key(character, hold)
            elif action == "screenshot":
                capture(value)
            elif action == "note":
                pass
            if step.get("capture_after", False):
                capture(f"step-{index:02d}-{action}")
            delay_after = float(step.get("delay_after", 0.25 if action in
                                        ("click", "double_click", "key", "chord",
                                         "text") else 0))
            if delay_after:
                wait_and_capture(delay_after, f"step-{index:02d}-delay")

        wait_and_capture(spec.observation_seconds, "observation")
        capture("final")
        status = "automation-complete"
    except Exception as error:
        error_text = f"{type(error).__name__}: {error}"
        events.write("failure", error=error_text)
        try:
            capture("failure")
        except Exception as capture_error:
            events.write("capture_failure", error=str(capture_error))
    finally:
        if client is not None:
            client.connection.close()
        if process is not None and process.poll() is None:
            try:
                quit_qemu(monitor_socket)
            except OSError as quit_error:
                events.write("monitor_quit_failure", error=str(quit_error))
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if qemu_log is not None:
            qemu_log.close()
        socket_context.cleanup()

    exit_code = process.returncode if process is not None else None
    elapsed = round(time.monotonic() - started, 3)
    result = {
        "run_id": spec.run_id,
        "game_id": spec.game_id,
        "name": spec.name,
        "mode": spec.mode,
        "status": status,
        "error": error_text,
        "started_at": wall_started,
        "finished_at": utc_now(),
        "elapsed_seconds": elapsed,
        "qemu_exit_code": exit_code,
        "clone_method": clone_method,
        "screenshots": capture_count,
        "evidence": str(run_dir),
    }
    write_json(run_dir / "result.json", result)
    write_json(run_dir / "review.json", {
        "run_id": spec.run_id,
        "review_status": "not-reviewed",
        "launch": None,
        "menus": None,
        "gameplay": None,
        "visual_correctness": None,
        "input": None,
        "audio": None,
        "stability": None,
        "gxmetal_profile_observed": None,
        "notes": "",
    })
    events.write("finished", status=status, qemu_exit_code=exit_code)
    events.close()
    if clone.exists() and (not args.keep_disks and
                           (status == "automation-complete" or
                            args.discard_failed_disks)):
        clone.unlink()
        result["disk_retained"] = False
    else:
        result["disk_retained"] = clone.exists()
    write_json(run_dir / "result.json", result)
    return result


def parse_modes(value: str) -> tuple[str, ...]:
    modes = tuple(part.strip() for part in value.split(",") if part.strip())
    if not modes or any(mode not in VALID_MODES for mode in modes):
        raise argparse.ArgumentTypeError(
            f"modes must be a comma-separated subset of {VALID_MODES}")
    return tuple(dict.fromkeys(modes))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run parallel, isolated GXMetal game compatibility sessions")
    parser.add_argument("base_disk", type=Path,
                        help="immutable source Mac OS raw disk image")
    parser.add_argument("manifest", type=Path,
                        help="JSON game/interaction manifest")
    parser.add_argument("--output", type=Path,
                        help="new evidence directory (default: a directory in /tmp)")
    parser.add_argument("--qemu", type=Path,
                        help="GXMetal QEMU (default: vendor/qemu/build/qemu-system-ppc)")
    parser.add_argument("--loader", type=Path,
                        help="NDRV loader (default: shared/ndrvloader)")
    parser.add_argument("--firmware", type=Path,
                        help="firmware directory (default: vendor/qemu/pc-bios)")
    parser.add_argument("--tools-cd", type=Path,
                        help="optional read-only ClassicMac Tools CD at IDE index 3")
    parser.add_argument("--jobs", type=int, default=2,
                        help="maximum simultaneous QEMU instances (default: 2)")
    parser.add_argument("--modes", type=parse_modes, default=("gxmetal",),
                        help="gxmetal, software, or gxmetal,software")
    parser.add_argument("--ram-mb", type=int, default=512)
    parser.add_argument("--cpu", default="7400")
    parser.add_argument("--accel", default="tcg,tb-size=512")
    parser.add_argument("--start-timeout", type=float, default=15.0)
    parser.add_argument("--keep-disks", action="store_true",
                        help="retain every modified per-run disk clone")
    parser.add_argument("--discard-failed-disks", action="store_true",
                        help="also discard clones from failed automation runs")
    parser.add_argument("--skip-base-hash", action="store_true",
                        help="verify base size/mtime only instead of SHA-256")
    parser.add_argument("--skip-media-hash", action="store_true",
                        help="record media metadata without computing SHA-256")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate and print the execution plan without writing files")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    source_disk = args.base_disk.expanduser().resolve()
    manifest = args.manifest.expanduser().resolve()
    qemu = (args.qemu.expanduser().resolve() if args.qemu else
            root / "vendor/qemu/build/qemu-system-ppc")
    loader = (args.loader.expanduser().resolve() if args.loader else
              root / "shared/ndrvloader")
    firmware = (args.firmware.expanduser().resolve() if args.firmware else
                root / "vendor/qemu/pc-bios")
    tools_cd = args.tools_cd.expanduser().resolve() if args.tools_cd else None

    for label, path, kind in (
        ("base disk", source_disk, "file"),
        ("manifest", manifest, "file"),
        ("QEMU", qemu, "file"),
        ("NDRV loader", loader, "file"),
        ("firmware", firmware, "directory"),
    ):
        exists = path.is_dir() if kind == "directory" else path.is_file()
        if not exists:
            parser.error(f"{label} does not exist: {path}")
    if tools_cd is not None and not tools_cd.is_file():
        parser.error(f"Tools CD does not exist: {tools_cd}")
    if not os.access(qemu, os.X_OK):
        parser.error(f"QEMU is not executable: {qemu}")
    if args.jobs <= 0 or args.ram_mb <= 0 or args.start_timeout <= 0:
        parser.error("jobs, RAM, and start timeout must be positive")

    try:
        specs = load_manifest(manifest, args.modes)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))

    if args.dry_run:
        plan = {
            "base_disk": str(source_disk),
            "manifest": str(manifest),
            "jobs": args.jobs,
            "runs": [
                {
                    "run_id": spec.run_id,
                    "name": spec.name,
                    "mode": spec.mode,
                    "cdrom": str(spec.cdrom) if spec.cdrom else None,
                    "steps": len(spec.steps),
                }
                for spec in specs
            ],
        }
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0

    if args.output:
        output = args.output.expanduser().resolve()
        try:
            output.mkdir(parents=True, exist_ok=False)
        except FileExistsError:
            parser.error(f"output directory already exists: {output}")
    else:
        output = Path(tempfile.mkdtemp(prefix="gxmetal-game-sweep-", dir="/tmp"))

    base_before = file_record(source_disk, not args.skip_base_hash)
    media_paths = sorted({spec.cdrom for spec in specs if spec.cdrom is not None})
    media_records = [file_record(path, not args.skip_media_hash)
                     for path in media_paths]
    for spec in specs:
        if spec.cdrom is not None and spec.source_sha256 is not None:
            record = next(item for item in media_records
                          if item["path"] == str(spec.cdrom))
            actual = record.get("sha256")
            if actual is None:
                parser.error("source_sha256 requires media hashing")
            if actual != spec.source_sha256:
                parser.error(f"media SHA-256 mismatch for {spec.game_id}")

    session = {
        "schema": SCHEMA_VERSION,
        "started_at": utc_now(),
        "base_before": base_before,
        "manifest": str(manifest),
        "manifest_sha256": sha256_file(manifest),
        "qemu": file_record(qemu),
        "qemu_version": captured_command([str(qemu), "--version"], root),
        "loader": file_record(loader),
        "firmware": str(firmware),
        "tools_cd": file_record(tools_cd) if tools_cd else None,
        "media": media_records,
        "jobs": args.jobs,
        "modes": args.modes,
        "runs": [spec.run_id for spec in specs],
        "host": {
            "platform": platform.platform(),
            "python": sys.version,
        },
        "repository": {
            "head": captured_command(["git", "rev-parse", "HEAD"], root),
            "status": captured_command(["git", "status", "--short"], root),
        },
    }
    shutil.copyfile(manifest, output / "manifest.json")
    write_json(output / "session.json", session)
    vnc_module = load_vnc_module(root)
    results: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {
            executor.submit(execute_run, root, source_disk, qemu, loader,
                            firmware, tools_cd, output, spec, vnc_module, args): spec
            for spec in specs
        }
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            print(f"[{result['status']}] {result['run_id']}: {result['evidence']}",
                  flush=True)

    base_after = file_record(source_disk, not args.skip_base_hash)
    integrity_fields = ("size", "mtime_ns", "sha256")
    base_unchanged = all(
        base_before.get(field) == base_after.get(field)
        for field in integrity_fields if field in base_before or field in base_after
    )
    session.update({
        "finished_at": utc_now(),
        "base_after": base_after,
        "base_unchanged": base_unchanged,
        "results": sorted(results, key=lambda item: item["run_id"]),
    })
    write_json(output / "session.json", session)
    write_json(output / "summary.json", session["results"])
    if not base_unchanged:
        raise RuntimeError("base disk changed during the sweep")
    failures = [result for result in results
                if result["status"] != "automation-complete"]
    print(f"Evidence: {output}")
    print(f"Base image unchanged: yes ({'metadata' if args.skip_base_hash else 'SHA-256'})")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
