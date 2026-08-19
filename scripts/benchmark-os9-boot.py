#!/usr/bin/env python3
"""Measure Mac OS 9 boot time using a disposable disk-image clone.

The benchmark launches the repository's Power Mac QEMU with the same core
devices as ClassicMac, polls QEMU's framebuffer through the monitor socket,
and stops when the Finder menu bar is fully visible.  The source disk is never
attached or written.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
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
    return dark >= FINDER_DARK_PIXELS and bright >= FINDER_BRIGHT_PIXELS


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
    shared_folder: Path | None,
    icount: str | None,
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
        "-drive",
        f"file={disk},format=raw,media=disk"
        + (f",cache={disk_cache}" if disk_cache else ""),
        "-drive", "if=ide,index=3,media=cdrom,id=cd0,readonly=on",
        "-drive", "if=ide,index=2,media=cdrom,id=tools0,readonly=on",
        "-trace", f"enable=pmac_ide_completion,file={scratch / 'ide.trace'}",
    ]
    if minimal_devices:
        command += ["-nic", "none"]
    else:
        command += [
            "-blockdev",
            f"driver=file,node-name=classicmac-tools-file,filename={tools},read-only=on",
            "-blockdev",
            "driver=raw,node-name=classicmac-tools,file=classicmac-tools-file,read-only=on",
            "-device", "virtio-blk-pci,drive=classicmac-tools",
            "-nic", "user,model=sungem",
            "-device", f"loader,addr=0x4000000,file={loader}",
            "-prom-env", "boot-command=init-program go",
            "-device", "virtio-tablet-pci",
        ]
    if shared_folder is not None and not minimal_devices:
        command += [
            "-device", "virtio-9p-pci,fsdev=share0,mount_tag=Shared",
            "-fsdev",
            f"local,id=share0,security_model=none,path={shared_folder}",
        ]
    if icount is not None:
        command += ["-icount", icount]
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
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument("--shared-folder", type=Path)
    parser.add_argument("--icount")
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
    if args.poll_interval <= 0 or args.timeout <= 0:
        parser.error("timeout and poll interval must be positive")

    source_before = source.stat()
    qemu = (
        args.qemu.expanduser().resolve()
        if args.qemu
        else root / "vendor/qemu/build/qemu-system-ppc"
    )
    if not qemu.is_file():
        parser.error(f"QEMU binary does not exist: {qemu}")
    scratch = Path(tempfile.mkdtemp(prefix="classicmac-os9-boot-", dir="/tmp"))
    disk = scratch / "disk.img"
    clone_disk(source, disk)
    command = qemu_command(
        root, qemu, disk, scratch, args.dma_delay_ns, args.ram_mb,
        args.disk_cache, args.accel, args.cpu,
        args.shared_folder.expanduser().resolve() if args.shared_folder else None,
        args.icount,
        args.extra_qemu_arg,
        args.minimal_devices,
    )
    log = (scratch / "qemu.log").open("wb")
    started = time.monotonic()
    process = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
    finder_seconds: float | None = None
    milestones: dict[str, float] = {}
    failure: Exception | None = None
    monitor_path = scratch / "monitor.sock"
    frame_path = scratch / "frame.ppm"

    try:
        wait_for_monitor(monitor_path, process, 5.0)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as monitor:
            monitor.connect(str(monitor_path))
            receive_until_prompt(monitor)
            deadline = started + args.timeout
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
        "minimal_devices": args.minimal_devices,
        "qemu": str(qemu),
        "graceful_shutdown": args.graceful_shutdown,
        "evidence": str(scratch),
    }
    result.update({name: round(value, 3) for name, value in milestones.items()})
    print(json.dumps(result, sort_keys=True))
    if not args.keep:
        shutil.rmtree(scratch)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
