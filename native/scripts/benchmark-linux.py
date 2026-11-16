#!/usr/bin/env python3
"""Compare two AppImages on an unlocked X11 desktop using disposable Macs.

Requires python3-psutil, python3-pil and python3-xlib. Measures actual visible
Finder/menu pixels after XTest input, not an internal renderer or QMP reply.
Run from an otherwise idle QA desktop. Images use identical sparse clones;
the caller must supply a stopped, disposable source machine.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
import uuid

from PIL import Image, ImageChops
import psutil
from Xlib import X, XK, display
from Xlib.ext import xtest


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction + .5))]


class Desktop:
    def __init__(self):
        self.connection = display.Display()
        self.root = self.connection.screen().root
        self.clients = self.connection.intern_atom("_NET_CLIENT_LIST")
        self.pid = self.connection.intern_atom("_NET_WM_PID")

    def windows(self):
        clients = self.root.get_full_property(self.clients, X.AnyPropertyType)
        for identity in clients.value if clients else []:
            window = self.connection.create_resource_object("window", identity)
            try:
                pid = window.get_full_property(self.pid, X.AnyPropertyType)
                yield window, int(pid.value[0]) if pid else 0, window.get_wm_name()
            except Exception:
                continue

    def capture(self, window, box):
        origin = self.root.translate_coords(window, 0, 0)
        x, y, width, height = box
        data = self.root.get_image(origin.x + x, origin.y + y, width, height,
                                   X.ZPixmap, 0xffffffff)
        assert data.depth == 24 and len(data.data) == width * height * 4
        return Image.frombytes("RGB", (width, height), data.data, "raw", "BGRX")

    def press(self, window, x, y):
        origin = self.root.translate_coords(window, 0, 0)
        xtest.fake_input(self.connection, X.MotionNotify, x=origin.x + x, y=origin.y + y)
        xtest.fake_input(self.connection, X.ButtonPress, 1)
        self.connection.sync()

    def release(self):
        xtest.fake_input(self.connection, X.ButtonRelease, 1)
        self.connection.sync()

    def click(self, window, x, y):
        self.press(window, x, y)
        time.sleep(.05)
        self.release()

    def escape(self):
        code = self.connection.keysym_to_keycode(XK.string_to_keysym("Escape"))
        xtest.fake_input(self.connection, X.KeyPress, code)
        self.connection.sync()
        time.sleep(.06)
        xtest.fake_input(self.connection, X.KeyRelease, code)
        self.connection.sync()


def finder_bar(image):
    # Finder menus occupy the top of the guest; a Happy Mac, firmware clear
    # and empty dither lack this mostly-light row of dark menu lettering.
    pixels = list(image.getdata())
    light = sum(min(px) > 185 for px in pixels)
    dark = sum(max(px) < 55 for px in pixels)
    return light > len(pixels) * .65 and 100 < dark < len(pixels) * .3


def run(desktop, package, source, output, hold_seconds):
    output.mkdir()
    clone = output / "Bench.classic"
    subprocess.run(["cp", "-a", "--reflink=auto", "--sparse=always", str(source), str(clone)], check=True)
    config = json.loads((clone / "config.json").read_text())
    config.update(id=str(uuid.uuid4()), name="Bench " + output.name)
    # Keep the same source geometry and device defaults in both versions.
    (clone / "config.json").write_text(json.dumps(config, indent=2) + "\n")
    profile = output / "profile"
    profile.mkdir()
    (profile / "view.ini").write_text(f"[display]\n{config['id']}/scale=1\n")
    machines = output / "machines"
    machines.mkdir()
    recipe = {"output": str(output), "steps": [
        {"action": "open", "folder": str(clone)}, {"action": "start"},
        {"action": "guestReady"},
        {"action": "wait", "milliseconds": hold_seconds * 1000, "timeout": (hold_seconds + 10) * 1000},
        {"action": "capture", "window": "guest", "name": "steady", "nativeScreen": True},
        {"action": "pause"},
        {"action": "capture", "window": "guest", "name": "paused"},
        {"action": "wait", "milliseconds": 7000},
        {"action": "resume"}, {"action": "shutdown"},
        {"action": "stopped", "timeout": 60000}]}
    path = output / "recipe.json"
    path.write_text(json.dumps(recipe, indent=2) + "\n")
    env = dict(os.environ, CLASSICMAC_QUALIFY="1", CLASSICMAC_TEST_HOME=str(profile),
               CLASSICMAC_TEST_DOCUMENTS=str(machines), QT_SCALE_FACTOR="1")
    for name in ("CLASSICMAC_RUNTIME", "QT_QPA_PLATFORM", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI"):
        env.pop(name, None)
    started = time.monotonic()
    log = (output / "app.log").open("w")
    process = subprocess.Popen([str(package), "--qualify", str(path)], env=env,
                               stdout=log, stderr=subprocess.STDOUT)
    parent = psutil.Process(process.pid)
    native = engine = guest = None
    home_at = engine_at = finder_at = None
    ready_samples = 0
    deadline = started + 65
    try:
        while time.monotonic() < deadline:
            assert process.poll() is None, "The app exited before Finder"
            descendants = parent.children(recursive=True)
            pids = {process.pid} | {p.pid for p in descendants}
            for item in descendants + [parent]:
                try:
                    if Path(item.exe()).name in ("qemu-system-m68k", "qemu-system-ppc"):
                        engine = item
                        if engine_at is None:
                            engine_at = time.monotonic()
                except psutil.Error:
                    pass
            for window, pid, title in desktop.windows():
                if pid not in pids:
                    continue
                native = psutil.Process(pid)
                if title == "ClassicMac" and home_at is None:
                    home_at = time.monotonic()
                if title == config["name"]:
                    guest = window
            if guest is not None and engine_at is not None:
                bar = desktop.capture(guest, (26, 22, 290, 19))
                ready_samples = ready_samples + 1 if finder_bar(bar) else 0
                if ready_samples >= 3:
                    finder_at = time.monotonic()
                    desktop.capture(guest, (6, 22, 320, 240)).save(output / "finder-visible.png")
                    break
            time.sleep(.04)
        assert finder_at is not None and native and engine, "Visible Finder did not appear within 65 seconds"
        print(output.name, "Finder", round(finder_at - engine_at, 3), "seconds", flush=True)

        # Sample an idle desktop before opening menus. CPU is percent of one
        # logical core; RSS includes shared libraries and is not private memory.
        cpu_before = [sum(p.cpu_times()[:2]) for p in (native, engine)]
        idle_start = time.monotonic()
        time.sleep(6)
        idle_elapsed = time.monotonic() - idle_start
        cpu = [(sum(p.cpu_times()[:2]) - before) / idle_elapsed * 100
               for p, before in zip((native, engine), cpu_before)]
        rss = [p.memory_info().rss for p in (native, engine)]
        desktop.click(guest, 306, 222)
        time.sleep(.2)
        latencies = []
        for index in range(8):
            desktop.escape()
            desktop.press(guest, 306, 222)
            time.sleep(.12)
            desktop.release()
            time.sleep(.15)
            before = desktop.capture(guest, (6, 44, 230, 190))
            tick = time.monotonic()
            # System 7 menus track while the button is held; an instantaneous
            # down/up can open and close between display refreshes.
            desktop.press(guest, 22, 31)
            changed = None
            while time.monotonic() - tick < 2:
                current = desktop.capture(guest, (6, 44, 230, 190))
                delta = ImageChops.difference(before, current).convert("L").histogram()
                if sum(delta[12:]) > 500:
                    changed = (time.monotonic() - tick) * 1000
                    if index == 0:
                        current.save(output / "menu-visible.png")
                    break
                time.sleep(.01)
            desktop.release()
            if changed is None:
                before.save(output / "menu-before.png")
                current.save(output / "menu-unresponsive.png")
                geometry = guest.get_geometry()
                desktop.capture(guest, (0, 0, geometry.width, geometry.height)).save(output / "window-unresponsive.png")
                pointer = desktop.root.query_pointer()
                (output / "input-diagnostic.txt").write_text(str(pointer) + "\n" + str(desktop.connection.get_input_focus()) + "\n")
            assert changed is not None, "A real guest menu did not respond to XTest input"
            latencies.append(changed)
        desktop.escape()
        report = {"package": package.name, "machineFamily": config["machineFamily"],
                  "graphicsCard": config.get("graphicsCard"), "sourceConfig": config,
                  "appVisibleMs": (home_at - started) * 1000 if home_at else None,
                  "finderFromEngineMs": (finder_at - engine_at) * 1000,
                  "inputToVisibleMenuMs": latencies,
                  "idleSampleSeconds": idle_elapsed, "frontendIdleOneCorePercent": cpu[0],
                  "engineIdleOneCorePercent": cpu[1], "frontendRSSBytes": rss[0], "engineRSSBytes": rss[1],
                  "display": "GNOME X11 desktop root pixels, 1x, 1920x1440; virtual llvmpipe graphics",
                  "hostLoadBefore": os.getloadavg(), "menuSamples": len(latencies)}
        pause_deadline = time.monotonic() + hold_seconds + 20
        while not (output / "paused.json").exists():
            assert process.poll() is None and time.monotonic() < pause_deadline, "The pause sample did not become ready"
            time.sleep(.05)
        paused_before = sum(engine.cpu_times()[:2])
        paused_start = time.monotonic()
        time.sleep(5)
        report["pausedEngineOneCorePercent"] = (sum(engine.cpu_times()[:2]) - paused_before) / (time.monotonic() - paused_start) * 100
        process.wait(timeout=hold_seconds + 80)
        result = json.loads((output / "result.json").read_text())
        assert process.returncode == 0 and result["passed"], result.get("error")
        report["cleanShutdown"] = True
        (output / "performance.json").write_text(json.dumps(report, indent=2) + "\n")
        return report
    finally:
        desktop.release()
        if process.poll() is None:
            # A failed QA run owns only this process tree and disposable disk.
            for child in parent.children(recursive=True):
                try:
                    child.terminate()
                except psutil.Error:
                    pass
            process.terminate()
            try:
                process.wait(10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--candidate-only", action="store_true",
                        help="Measure a configuration that the previous release cannot boot")
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--machine", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runs", type=int, default=3, choices=range(1, 6))
    parser.add_argument("--hold-seconds", type=int, default=60)
    args = parser.parse_args()
    output = args.output.resolve()
    assert not output.exists(), "Choose a new output directory"
    output.mkdir(parents=True)
    if not args.candidate_only and args.baseline is None:
        parser.error("Supply --baseline or explicitly use --candidate-only")
    packages = {"candidate": args.candidate.resolve(strict=True)}
    if not args.candidate_only:
        packages["baseline"] = args.baseline.resolve(strict=True)
    source = args.machine.resolve(strict=True)
    desktop = Desktop()
    rows = []
    for number in range(args.runs):
        order = ("candidate",) if args.candidate_only else (("baseline", "candidate") if number % 2 == 0 else ("candidate", "baseline"))
        for version in order:
            row = run(desktop, packages[version], source, output / f"{number + 1}-{version}", args.hold_seconds)
            row.update(version=version, repetition=number + 1)
            rows.append(row)
            (output / "runs.json").write_text(json.dumps(rows, indent=2) + "\n")
    summary = {}
    for version, package in packages.items():
        selected = [r for r in rows if r["version"] == version]
        latencies = [t for r in selected for t in r["inputToVisibleMenuMs"]]
        summary[version] = {"package": package.name, "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
                            "runs": len(selected), "menuSamples": len(latencies),
                            "finderMedianMs": statistics.median(r["finderFromEngineMs"] for r in selected),
                            "inputMedianMs": statistics.median(latencies), "inputP95Ms": percentile(latencies, .95),
                            "frontendIdlePercent": statistics.median(r["frontendIdleOneCorePercent"] for r in selected),
                            "engineIdlePercent": statistics.median(r["engineIdleOneCorePercent"] for r in selected),
                            "pausedEnginePercent": statistics.median(r["pausedEngineOneCorePercent"] for r in selected),
                            "frontendRSSMiB": statistics.median(r["frontendRSSBytes"] for r in selected) / 1048576}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == "__main__":
    main()
