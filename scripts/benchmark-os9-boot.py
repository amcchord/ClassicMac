#!/usr/bin/env python3
"""Measure Mac OS 9 startup and resume using a disposable disk-image clone.

The benchmark launches the repository's Power Mac QEMU with the same core
devices as ClassicMac, polls QEMU's framebuffer through the monitor socket,
and stops when the Finder menu bar is fully visible.  The source disk is never
attached or written.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


FINDER_DARK_PIXELS = 800
FINDER_BRIGHT_PIXELS = 10_000


def receive_until_prompt(connection: socket.socket) -> bytes:
    response = bytearray()
    while not response.endswith(b"(qemu) "):
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError("QEMU monitor closed unexpectedly")
        response.extend(chunk)
    return bytes(response)


def hmp_command(connection: socket.socket, command: str) -> bytes:
    connection.sendall(f"{command}\n".encode("utf-8"))
    response = receive_until_prompt(connection)
    if b"Error" in response:
        raise RuntimeError(response.decode("utf-8", "replace"))
    return response


def classicmac_clock_status(response: bytes) -> str:
    match = re.search(rb"classicmac clock: ([a-z -]+)", response)
    if match is None:
        return "unknown"
    return match.group(1).decode("ascii")


def classicmac_handoff_host_ns(response: bytes) -> int | None:
    match = re.search(rb"classicmac handoff host ns: (-?\d+)", response)
    if match is None:
        return None
    value = int(match.group(1))
    return value if value >= 0 else None


def classicmac_current_host_ns(response: bytes) -> int | None:
    match = re.search(rb"classicmac current host ns: (-?\d+)", response)
    if match is None:
        return None
    value = int(match.group(1))
    return value if value >= 0 else None


def timebase_from_registers(connection: socket.socket) -> int:
    response = hmp_command(connection, "info registers")
    match = re.search(rb"\bTB\s+\d+\s+(\d+)\s+DECR\b", response)
    if match is None:
        raise RuntimeError("PowerPC timebase is missing from info registers")
    return int(match.group(1))


def capture_frame(connection: socket.socket, path: Path) -> tuple[int, int, bytes]:
    connection.sendall(f"screendump {path}\n".encode("utf-8"))
    response = receive_until_prompt(connection)
    if b"Error" in response:
        raise RuntimeError(response.decode("utf-8", "replace"))

    data = path.read_bytes()
    first, dimensions, maximum, rgb = data.split(b"\n", 3)
    if first != b"P6" or maximum != b"255":
        raise RuntimeError(f"unexpected screendump format in {path}")
    width, height = (int(value) for value in dimensions.split())
    if len(rgb) != width * height * 3:
        raise RuntimeError(f"truncated screendump in {path}")
    return width, height, rgb


def finder_is_ready(width: int, rgb: bytes) -> bool:
    band_height = 20
    band = memoryview(rgb)[: width * band_height * 3]
    dark = 0
    bright = 0
    for offset in range(0, len(band), 3):
        red, green, blue = band[offset : offset + 3]
        if max(red, green, blue) < 50:
            dark += 1
        if min(red, green, blue) > 170:
            bright += 1
    total = width * band_height
    dark_required = min(FINDER_DARK_PIXELS, max(200, total // 25))
    bright_required = max(2_000, total // 2)
    return dark >= dark_required and bright >= bright_required


def welcome_is_visible(width: int, height: int, rgb: bytes) -> bool:
    bright = 0
    for y in range(height // 2 - 190, height // 2 + 80):
        for x in range(width // 2 - 220, width // 2 + 220):
            offset = (y * width + x) * 3
            if min(rgb[offset : offset + 3]) > 220:
                bright += 1
    return bright > 20_000


def desktop_is_visible(width: int, rgb: bytes) -> bool:
    offset = (30 * width + 10) * 3
    red, green, blue = rgb[offset : offset + 3]
    return blue > red + 20 and blue > green + 20


def changed_pixel_fraction(
    before: bytes,
    after: bytes,
    width: int,
    height: int,
    right: int,
    bottom: int,
) -> float:
    changed = 0
    pixels = min(width, right) * min(height, bottom)
    for y in range(min(height, bottom)):
        for x in range(min(width, right)):
            offset = (y * width + x) * 3
            if any(
                abs(before[offset + channel] - after[offset + channel]) > 8
                for channel in range(3)
            ):
                changed += 1
    return changed / pixels if pixels else 0.0


def clone_disk(source: Path, destination: Path) -> None:
    result = subprocess.run(
        ["cp", "-c", str(source), str(destination)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        shutil.copyfile(source, destination)


def qemu_command(
    root: Path,
    qemu: Path,
    disk: Path,
    scratch: Path,
    dma_delay_ns: int,
    ram_mb: int,
    disk_cache: str | None,
    accel: str,
    cpu: str,
    boot_disk_virtio: bool,
    boot_cd: Path | None,
    shared_folder: Path | None,
    icount: str | None,
    load_state: Path | None,
    extra_qemu_args: list[str],
    minimal_devices: bool,
) -> list[str]:
    firmware = root / "vendor/qemu/pc-bios"
    tools = root / "dist/ClassicMacTools.iso"
    loader = root / "shared/ndrvloader"
    command = [
        str(qemu),
        "-accel", accel,
        "-M", "mac99,via=cuda,audiodev=snd0",
        "-cpu", cpu,
        "-m", str(ram_mb),
        "-L", str(firmware),
        "-display", "none",
        "-vnc", f"unix:{scratch / 'vnc.sock'}",
        "-vga", "std",
        "-global", "VGA.host-resize=on",
        "-global", "VGA.vgamem_mb=64",
        "-global", "VGA.packed-lowbpp=on",
        "-global", "VGA.untracked-vram=on",
        "-global", "VGA.hardware-cursor=on",
        "-global", "VGA.gxmetal=on",
        "-global", f"macio-ide.dma-completion-delay-ns={dma_delay_ns}",
        "-prom-env", "output-device=ttya",
        "-g", "1024x768x15",
        "-name", f"ClassicMac OS 9 Boot Benchmark ({dma_delay_ns} ns)",
        "-audiodev", "none,id=snd0",
        "-serial", f"file:{scratch / 'serial.log'}",
        "-monitor", f"unix:{scratch / 'monitor.sock'},server=on,wait=off",
        "-action", "reboot=shutdown",
        "-trace", f"enable=pmac_ide_completion,file={scratch / 'ide.trace'}",
    ]
    disk_cache_option = f",cache={disk_cache}" if disk_cache else ""
    if boot_disk_virtio:
        command += [
            "-blockdev",
            f"driver=file,node-name=classicmac-boot-file,filename={disk}",
            "-blockdev",
            "driver=raw,node-name=classicmac-boot,file=classicmac-boot-file",
            "-device", "virtio-blk-pci,drive=classicmac-boot",
            "-prom-env", "boot-device=virtio0:\\\\:tbxi",
        ]
    else:
        command += [
            "-drive",
            f"file={disk},format=raw,media=disk{disk_cache_option}",
        ]
    user_disc_index = 2 if boot_cd is not None else 3
    tools_disc_index = 3 if boot_cd is not None else 2
    user_disc = f"if=ide,index={user_disc_index},media=cdrom,id=cd0,readonly=on"
    if boot_cd is not None:
        user_disc += f",file={boot_cd},format=raw"
    command += [
        "-drive", user_disc,
        "-drive",
        f"if=ide,index={tools_disc_index},media=cdrom,id=tools0,readonly=on",
    ]
    if boot_cd is not None:
        command += [
            "-blockdev",
            f"driver=file,node-name=classicmac-cd-file,filename={boot_cd},read-only=on",
            "-blockdev",
            "driver=raw,node-name=classicmac-cd-boot,file=classicmac-cd-file,read-only=on",
            "-device", "virtio-blk-pci,drive=classicmac-cd-boot",
            "-prom-env",
            f"boot-device=virtio{1 if boot_disk_virtio else 0}:\\\\:tbxi",
        ]
    if minimal_devices:
        command += ["-nic", "none"]
    else:
        if boot_cd is None:
            command += [
                "-blockdev",
                f"driver=file,node-name=classicmac-tools-file,filename={tools},read-only=on",
                "-blockdev",
                "driver=raw,node-name=classicmac-tools,file=classicmac-tools-file,read-only=on",
                "-device", "virtio-blk-pci,drive=classicmac-tools",
            ]
        command += [
            "-nic", "user,model=sungem",
            "-device", f"loader,addr=0x4000000,file={loader}",
            "-prom-env", "boot-command=init-program go",
            "-device", "virtio-tablet-pci",
        ]
    if (boot_cd is not None or boot_disk_virtio) and minimal_devices:
        command += [
            "-device", f"loader,addr=0x4000000,file={loader}",
            "-prom-env", "boot-command=init-program go",
        ]
    if shared_folder is not None and not minimal_devices:
        command += [
            "-device", "virtio-9p-pci,fsdev=share0,mount_tag=Shared",
            "-fsdev",
            f"local,id=share0,security_model=none,path={shared_folder}",
        ]
    if icount is not None:
        command += ["-icount", icount]
    if load_state is not None:
        command += ["-incoming", f"file:{load_state}"]
    command += extra_qemu_args
    return command


def wait_for_monitor(path: Path, process: subprocess.Popen[bytes], timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited before creating its monitor ({process.returncode})")
        if path.exists():
            return
        time.sleep(0.01)
    raise RuntimeError("timed out waiting for the QEMU monitor")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("disk", type=Path, help="source Mac OS 9 raw disk image")
    parser.add_argument(
        "--qemu",
        type=Path,
        help="QEMU binary (defaults to vendor/qemu/build/qemu-system-ppc)",
    )
    parser.add_argument("--dma-delay-ns", type=int, default=1_000_000)
    parser.add_argument("--ram-mb", type=int, default=512)
    parser.add_argument(
        "--disk-cache",
        choices=("none", "directsync", "writeback", "unsafe"),
        help="override QEMU's main-disk cache mode",
    )
    parser.add_argument("--accel", default="tcg,tb-size=512")
    parser.add_argument("--cpu", default="7400")
    parser.add_argument(
        "--boot-disk-virtio",
        action="store_true",
        help="boot the main disk through ClassicMac's Virtio block driver",
    )
    parser.add_argument(
        "--boot-cd",
        type=Path,
        help="boot the selected installer CD through ClassicMac's Virtio path",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument(
        "--poll-start-delay",
        type=float,
        default=0.0,
        help="let the guest run unobserved for this many seconds before framebuffer polling",
    )
    parser.add_argument(
        "--pause-resume-seconds",
        type=float,
        default=0.0,
        help="pause at Finder and verify the PowerPC timebase remains frozen",
    )
    parser.add_argument(
        "--idle-seconds",
        type=float,
        default=0.0,
        help="idle at Finder, verify the 25 MHz timebase, and test input afterward",
    )
    parser.add_argument("--shared-folder", type=Path)
    parser.add_argument("--icount")
    parser.add_argument(
        "--handoff-at-finder",
        action="store_true",
        help="restore real-time emulation through ClassicMac's QEMU handoff once Finder appears",
    )
    parser.add_argument(
        "--save-state",
        type=Path,
        help="save a resumable QEMU migration stream after Finder appears",
    )
    parser.add_argument(
        "--load-state",
        type=Path,
        help="resume a QEMU migration stream instead of executing a cold boot",
    )
    parser.add_argument(
        "--minimal-devices",
        action="store_true",
        help="omit ClassicMac Tools, networking, tablet, and folder sharing",
    )
    parser.add_argument(
        "--extra-qemu-arg",
        action="append",
        default=[],
        help="append one raw QEMU argument (use --extra-qemu-arg=-option)",
    )
    parser.add_argument("--keep", action="store_true")
    parser.add_argument(
        "--power-dialog",
        action="store_true",
        help="press the emulated ADB power key at Finder and save the result",
    )
    parser.add_argument(
        "--graceful-shutdown",
        action="store_true",
        help="confirm the ADB power dialog and retain a clean boot volume",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    source = args.disk.expanduser().resolve()
    if not source.is_file():
        parser.error(f"disk image does not exist: {source}")
    if args.dma_delay_ns < 0:
        parser.error("--dma-delay-ns must be nonnegative")
    if (args.poll_interval <= 0 or args.timeout <= 0 or
            args.poll_start_delay < 0 or args.pause_resume_seconds < 0 or
            args.idle_seconds < 0):
        parser.error(
            "timeout and poll interval must be positive; delays must be nonnegative"
        )

    source_before = source.stat()
    qemu = (
        args.qemu.expanduser().resolve()
        if args.qemu
        else root / "vendor/qemu/build/qemu-system-ppc"
    )
    if not qemu.is_file():
        parser.error(f"QEMU binary does not exist: {qemu}")
    boot_cd = args.boot_cd.expanduser().resolve() if args.boot_cd else None
    if boot_cd is not None and not boot_cd.is_file():
        parser.error(f"installer CD does not exist: {boot_cd}")
    save_state = args.save_state.expanduser().resolve() if args.save_state else None
    load_state = args.load_state.expanduser().resolve() if args.load_state else None
    if load_state is not None and not load_state.is_file():
        parser.error(f"saved state does not exist: {load_state}")
    scratch = Path(tempfile.mkdtemp(prefix="classicmac-os9-boot-", dir="/tmp"))
    disk = scratch / "disk.img"
    clone_disk(source, disk)
    command = qemu_command(
        root, qemu, disk, scratch, args.dma_delay_ns, args.ram_mb,
        args.disk_cache, args.accel, args.cpu, args.boot_disk_virtio, boot_cd,
        args.shared_folder.expanduser().resolve() if args.shared_folder else None,
        args.icount,
        load_state,
        args.extra_qemu_arg,
        args.minimal_devices,
    )
    log = (scratch / "qemu.log").open("wb")
    started_ns = time.monotonic_ns()
    started = started_ns / 1_000_000_000
    process = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
    finder_seconds: float | None = None
    milestones: dict[str, float] = {}
    pause_resume: dict[str, int | float] = {}
    handoff: dict[str, int | float | str] = {}
    idle: dict[str, int | float] = {}
    failure: Exception | None = None
    monitor_path = scratch / "monitor.sock"
    frame_path = scratch / "frame.ppm"

    try:
        wait_for_monitor(monitor_path, process, 5.0)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
            monitor.connect(str(monitor_path))
            receive_until_prompt(monitor)
            deadline = started + args.timeout
            poll_start = started + args.poll_start_delay
            if time.monotonic() < poll_start:
                time.sleep(poll_start - time.monotonic())
            while time.monotonic() < deadline:
                width, height, rgb = capture_frame(monitor, frame_path)
                elapsed = time.monotonic() - started
                checks = (
                    ("welcome_seconds", welcome_is_visible(width, height, rgb)),
                    ("desktop_seconds", desktop_is_visible(width, rgb)),
                )
                for name, reached in checks:
                    if reached and name not in milestones:
                        milestones[name] = elapsed
                        shutil.copyfile(frame_path, scratch / f"{name}.ppm")
                if finder_is_ready(width, rgb):
                    finder_seconds = elapsed
                    shutil.copyfile(frame_path, scratch / "finder.ppm")
                    break
                time.sleep(args.poll_interval)
            if finder_seconds is not None and args.handoff_at_finder:
                before = timebase_from_registers(monitor)
                requested_at = time.monotonic()
                response = hmp_command(monitor, "classicmac-boot-complete")
                initial_status = classicmac_clock_status(response)
                status = initial_status
                handoff_deadline = time.monotonic() + 2.0
                while status != "real-time":
                    if time.monotonic() >= handoff_deadline:
                        raise RuntimeError(
                            "instruction-count clock did not hand off within two seconds"
                        )
                    time.sleep(0.01)
                    response = hmp_command(monitor, "classicmac-boot-complete")
                    status = classicmac_clock_status(response)
                after = timebase_from_registers(monitor)
                observed_ns = time.monotonic_ns()
                handoff = {
                    "requested_seconds": round(requested_at - started, 3),
                    "completed_seconds": round(time.monotonic() - started, 3),
                    "timebase_delta": after - before,
                    "initial_status": initial_status,
                    "final_status": status,
                }
                handoff_host_ns = classicmac_handoff_host_ns(response)
                current_host_ns = classicmac_current_host_ns(response)
                if handoff_host_ns is not None and current_host_ns is not None:
                    handoff["auto_finder_seconds"] = round(
                        ((observed_ns - started_ns) -
                         (current_host_ns - handoff_host_ns)) /
                        1_000_000_000,
                        3,
                    )
            if finder_seconds is not None and args.idle_seconds > 0:
                before = timebase_from_registers(monitor)
                idle_started = time.monotonic()
                time.sleep(args.idle_seconds)
                after = timebase_from_registers(monitor)
                idle_elapsed = time.monotonic() - idle_started
                timebase_delta = after - before
                expected = 25_000_000 * idle_elapsed
                if not expected * 0.95 <= timebase_delta <= expected * 1.05:
                    raise RuntimeError(
                        "PowerPC timebase did not remain at its 25 MHz guest "
                        "rate while idle"
                    )

                width, height, before_input = capture_frame(monitor, frame_path)
                input_result = subprocess.run(
                    [
                        str(root / "scripts/gxmetal-vnc.py"),
                        "--unix-socket", str(scratch / "vnc.sock"),
                        "--click", "12,10",
                        "--delay", "0.5",
                    ],
                    cwd=root,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    check=False,
                )
                if input_result.returncode != 0:
                    raise RuntimeError(
                        "VNC input probe failed after idle: " + input_result.stdout
                    )
                after_width, after_height, after_input = capture_frame(
                    monitor, frame_path
                )
                if (after_width, after_height) != (width, height):
                    raise RuntimeError("framebuffer dimensions changed during input probe")
                changed_fraction = changed_pixel_fraction(
                    before_input, after_input, width, height, 240, 420
                )
                if changed_fraction < 0.01:
                    raise RuntimeError(
                        "Finder did not respond to the Apple-menu click after idle"
                    )
                shutil.copyfile(frame_path, scratch / "idle-input.ppm")
                idle = {
                    "idle_seconds": round(idle_elapsed, 3),
                    "timebase_delta": timebase_delta,
                    "timebase_hz": round(timebase_delta / idle_elapsed),
                    "input_changed_fraction": round(changed_fraction, 6),
                }
            if finder_seconds is not None and args.pause_resume_seconds > 0:
                hmp_command(monitor, "stop")
                before = timebase_from_registers(monitor)
                time.sleep(args.pause_resume_seconds)
                during = timebase_from_registers(monitor)
                hmp_command(monitor, "cont")
                resumed_at = time.monotonic()
                time.sleep(1.0)
                hmp_command(monitor, "stop")
                after = timebase_from_registers(monitor)
                resumed_seconds = time.monotonic() - resumed_at
                hmp_command(monitor, "cont")
                frozen_delta = during - before
                resumed_delta = after - during
                pause_resume = {
                    "pause_seconds": args.pause_resume_seconds,
                    "frozen_tb_delta": frozen_delta,
                    "resume_seconds": round(resumed_seconds, 3),
                    "resumed_tb_delta": resumed_delta,
                }
                if frozen_delta != 0:
                    raise RuntimeError(
                        f"PowerPC timebase advanced {frozen_delta} ticks while paused"
                    )
                expected = 25_000_000 * resumed_seconds
                if not expected * 0.5 <= resumed_delta <= expected * 2.0:
                    raise RuntimeError(
                        "PowerPC timebase did not resume at its 25 MHz guest rate"
                    )
            if finder_seconds is not None and (
                args.power_dialog or args.graceful_shutdown
            ):
                monitor.sendall(b"sendkey power\n")
                receive_until_prompt(monitor)
                time.sleep(1.0)
                capture_frame(monitor, frame_path)
                shutil.copyfile(frame_path, scratch / "power-dialog.ppm")
            if finder_seconds is not None and args.graceful_shutdown:
                monitor.sendall(b"sendkey ret\n")
                receive_until_prompt(monitor)
            elif finder_seconds is not None and save_state is not None:
                save_state.parent.mkdir(parents=True, exist_ok=True)
                migration_uri = f"file:{save_state}"
                monitor.sendall(
                    f"migrate {json.dumps(migration_uri)}\n".encode("utf-8")
                )
                response = receive_until_prompt(monitor)
                if b"error" in response.lower():
                    raise RuntimeError(response.decode("utf-8", "replace"))
                monitor.sendall(b"quit\n")
            else:
                monitor.sendall(b"quit\n")
    except Exception as error:  # Keep evidence and report after cleanup.
        failure = error
    finally:
        try:
            process.wait(timeout=15 if args.graceful_shutdown else 5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
        log.close()

    source_after = source.stat()
    if (source_before.st_size, source_before.st_mtime_ns) != (
        source_after.st_size, source_after.st_mtime_ns
    ):
        raise RuntimeError("source disk metadata changed during the benchmark")
    if failure is not None:
        raise failure
    if finder_seconds is None:
        raise RuntimeError(f"Finder was not ready within {args.timeout:.1f} seconds")

    trace_path = scratch / "ide.trace"
    completions = 0
    if trace_path.exists():
        with trace_path.open("rb") as trace:
            completions = sum(1 for _ in trace)
    result = {
        "finder_seconds": round(finder_seconds, 3),
        "dma_delay_ns": args.dma_delay_ns,
        "ide_dma_completions": completions,
        "artificial_dma_seconds": round(completions * args.dma_delay_ns / 1e9, 3),
        "ram_mb": args.ram_mb,
        "disk_cache": args.disk_cache or "default",
        "accel": args.accel,
        "cpu": args.cpu,
        "icount": args.icount,
        "handoff_at_finder": args.handoff_at_finder,
        "boot_disk_virtio": args.boot_disk_virtio,
        "boot_cd": str(boot_cd) if boot_cd is not None else None,
        "load_state": str(load_state) if load_state is not None else None,
        "save_state": str(save_state) if save_state is not None else None,
        "minimal_devices": args.minimal_devices,
        "qemu": str(qemu),
        "graceful_shutdown": args.graceful_shutdown,
        "evidence": str(scratch),
    }
    result.update({name: round(value, 3) for name, value in milestones.items()})
    if pause_resume:
        result["pause_resume"] = pause_resume
    if handoff:
        result["handoff"] = handoff
    if idle:
        result["idle"] = idle
    print(json.dumps(result, sort_keys=True))
    if not args.keep:
        shutil.rmtree(scratch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
