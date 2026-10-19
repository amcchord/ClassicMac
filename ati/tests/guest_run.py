#!/usr/bin/env python3
"""Run a ClassicMac Power Mac G4 machine headless and drive it for tests.

The argument sets mirror what ClassicMac launches (QEMURunner's Power Mac
arguments) with either display card:

  --card vga   std-VGA with ClassicMac's globals (host-resize, packed-lowbpp,
               untracked-vram, hardware-cursor, classicmac-boot-handoff) and
               the instruction-clock boot (-icount shift=4,sleep=off);
  --card ati   the emulated ATI Radeon R100 at WxH (EDID detailed timing via
               xres/yres/refresh_rate), boot frame 640x480x32, the display
               helper's -prom-env options, real-time clock (no icount).

The display is a private VNC Unix socket (nothing listens on the network);
frames come from HMP screendump. Subcommands keep QEMU running between
calls so a test can look, type and click:

  guest_run.py start --out DIR --disk IMG --card ati --size 1024x768 ...
  guest_run.py shot  --out DIR NAME            # DIR/NAME.png
  guest_run.py hmp   --out DIR 'info status'
  guest_run.py qom   --out DIR PATH PROPERTY   # e.g. /machine/peripheral/ati0 x-accel-stats
  guest_run.py click --out DIR X Y [--size WxH] [--button left] [--double]
  guest_run.py key   --out DIR KEY [KEY ...]   # QEMU qcodes, e.g. meta_l-q
  guest_run.py type  --out DIR TEXT
  guest_run.py stop  --out DIR                 # quit QEMU (and wait)
"""
import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time

EDID_MAX_CLOCK_KHZ = 162000          # ATI's Mac driver ignores faster timings
EDID_REFRESH_HZ = (75, 60, 50)


def edid_refresh(w, h):
    """QEMU EDID detailed-timing refresh within ATI's pixel-clock limit."""
    for hz in EDID_REFRESH_HZ:
        xblank, yblank = w * 35 // 100, h * 35 // 1000
        if hz * 1000 * (w + xblank) * (h + yblank) // 10000000 * 10 <= EDID_MAX_CLOCK_KHZ:
            return hz
    return None


def build_argv(a):
    w, h = (int(v) for v in a.size.split("x"))
    out = os.path.abspath(a.out)
    argv = [a.qemu, "-accel", "tcg,tb-size=512"]
    icount = a.icount if a.icount is not None else (a.card == "vga")
    if icount:
        argv += ["-icount", "shift=4,sleep=off"]
    argv += ["-M", "mac99,via=cuda,audiodev=snd0", "-cpu", "7400",
             "-m", str(a.ram), "-L", a.bios]
    if a.card == "vga":
        argv += ["-vga", "std",
                 "-global", "VGA.host-resize=on", "-global", "VGA.vgamem_mb=64",
                 "-global", "VGA.packed-lowbpp=on",
                 "-global", "VGA.untracked-vram=on",
                 "-global", "VGA.hardware-cursor=on"]
        if icount:
            argv += ["-global", "VGA.classicmac-boot-handoff=on"]
        boot = f"{w}x{h}x{a.depth}"
    else:
        dev = "ati-vga,id=ati0,model=r100,vgamem_mb=32,romfile=,guest_hwcursor=on"
        hz = edid_refresh(w, h)
        if hz is not None and (w, h) != (640, 480):
            dev += f",xres={w},yres={h},refresh_rate={hz * 1000}"
        argv += ["-vga", "none", "-device", dev]
        boot = "640x480x32"
        for opt in a.display_option:
            argv += ["-prom-env", f"{opt}={w}x{h}"]
    argv += ["-global", "macio-ide.dma-completion-delay-ns=0",
             "-prom-env", "output-device=ttya", "-g", boot,
             "-name", a.name, "-audiodev", "none,id=snd0",
             "-display", f"vnc=unix:{out}/vnc.sock",
             "-monitor", f"unix:{out}/hmp.sock,server=on,wait=off",
             "-qmp", f"unix:{out}/qmp.sock,server=on,wait=off",
             "-action", "reboot=shutdown",
             "-serial", f"file:{out}/serial.log",
             "-drive", f"file={a.disk},format=raw,media=disk",
             "-drive", "if=ide,index=3,media=cdrom,id=cd0,readonly=on",
             "-drive", "if=ide,index=2,media=cdrom,id=tools0,readonly=on",
             "-nic", "none"]
    if a.tools_cd:
        argv += ["-blockdev", f"driver=file,node-name=tools-file,filename={a.tools_cd},read-only=on",
                 "-blockdev", "driver=raw,node-name=tools,file=tools-file,read-only=on",
                 "-device", "virtio-blk-pci,drive=tools"]
    if a.ndrvloader:
        argv += ["-device", f"loader,addr=0x4000000,file={a.ndrvloader}",
                 "-prom-env", "boot-command=init-program go",
                 "-device", "virtio-tablet-pci"]
    argv += a.extra
    return argv


def hmp(out, command, timeout=30):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(timeout)
    s.connect(os.path.join(out, "hmp.sock"))
    buf = b""

    def until_prompt():
        nonlocal buf
        while b"(qemu) " not in buf:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
        text, _, buf = buf.partition(b"(qemu) ")
        return text

    until_prompt()
    s.sendall(command.encode() + b"\n")
    reply = until_prompt().decode(errors="replace")
    s.close()
    lines = reply.replace("\r", "").split("\n")
    return "\n".join(l for l in lines[1:] if l.strip()).strip()


class QMP:
    def __init__(self, out, timeout=30):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(timeout)
        self.s.connect(os.path.join(out, "qmp.sock"))
        self.f = self.s.makefile("rw")
        json.loads(self.f.readline())
        self.cmd("qmp_capabilities")

    def cmd(self, execute, **args):
        self.f.write(json.dumps({"execute": execute, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            msg = json.loads(self.f.readline())
            if "event" in msg:
                continue
            if "error" in msg:
                raise RuntimeError(f"{execute}: {msg['error']}")
            return msg.get("return")

    def close(self):
        self.f.close()
        self.s.close()


def shot(out, name):
    path = os.path.abspath(os.path.join(out, name + ".png"))
    reply = hmp(out, f"screendump {path} -f png")
    if reply:
        print(reply)
    return path


def cmd_start(a):
    os.makedirs(a.out, exist_ok=True)
    argv = build_argv(a)
    with open(os.path.join(a.out, "argv.json"), "w") as f:
        json.dump(argv, f, indent=1)
    env = dict(os.environ)
    for k, v in (kv.split("=", 1) for kv in a.env):
        env[k] = v
    log = open(os.path.join(a.out, "qemu.log"), "ab")
    p = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log,
                         stderr=subprocess.STDOUT, env=env,
                         start_new_session=True)
    with open(os.path.join(a.out, "qemu.pid"), "w") as f:
        f.write(str(p.pid))
    with open(os.path.join(a.out, "started"), "w") as f:
        f.write(str(time.time()))
    for _ in range(100):
        if os.path.exists(os.path.join(a.out, "hmp.sock")):
            break
        if p.poll() is not None:
            sys.exit(f"QEMU exited: {open(os.path.join(a.out, 'qemu.log')).read()[-2000:]}")
        time.sleep(0.1)
    print(f"started pid {p.pid}")


def abs_event(x, y, w, h):
    return [{"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / max(w - 1, 1))}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / max(h - 1, 1))}}]


def cmd_click(a):
    w, h = (int(v) for v in a.size.split("x"))
    q = QMP(a.out)
    try:
        q.cmd("input-send-event", events=abs_event(a.x, a.y, w, h))
        time.sleep(0.15)
        for _ in range(2 if a.double else 1):
            q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": a.button}}])
            time.sleep(a.hold)
            q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": a.button}}])
            time.sleep(0.12)
    finally:
        q.close()


def cmd_drag(a):
    w, h = (int(v) for v in a.size.split("x"))
    q = QMP(a.out)
    try:
        q.cmd("input-send-event", events=abs_event(a.x0, a.y0, w, h))
        time.sleep(0.2)
        q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
        time.sleep(0.3)
        steps = 12
        for i in range(1, steps + 1):
            q.cmd("input-send-event", events=abs_event(a.x0 + (a.x1 - a.x0) * i / steps,
                                                       a.y0 + (a.y1 - a.y0) * i / steps, w, h))
            time.sleep(0.05)
        time.sleep(0.4)
        q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
    finally:
        q.close()


def cmd_move(a):
    w, h = (int(v) for v in a.size.split("x"))
    q = QMP(a.out)
    try:
        q.cmd("input-send-event", events=abs_event(a.x, a.y, w, h))
    finally:
        q.close()


def cmd_stop(a):
    pid = int(open(os.path.join(a.out, "qemu.pid")).read())
    try:
        q = QMP(a.out, timeout=5)
        q.cmd("quit")
        q.close()
    except Exception as exc:
        print(f"QMP quit failed ({exc}); sending SIGTERM")
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    for _ in range(100):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            print("stopped")
            return
        time.sleep(0.1)
    os.kill(pid, signal.SIGKILL)
    print("killed")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("--out", required=True)
    s.add_argument("--qemu", required=True)
    s.add_argument("--bios", required=True)
    s.add_argument("--disk", required=True)
    s.add_argument("--card", choices=("vga", "ati"), default="vga")
    s.add_argument("--size", default="1024x768")
    s.add_argument("--depth", type=int, default=15)
    s.add_argument("--ram", type=int, default=512)
    s.add_argument("--name", default="Power Mac G4")
    s.add_argument("--icount", dest="icount", action="store_true", default=None)
    s.add_argument("--no-icount", dest="icount", action="store_false")
    s.add_argument("--display-option", action="append", default=[],
                   help="-prom-env NAME=WxH for a display helper (ATI only)")
    s.add_argument("--ndrvloader")
    s.add_argument("--tools-cd")
    s.add_argument("--env", action="append", default=[])
    s.add_argument("extra", nargs="*")
    for name in ("shot", "hmp", "qom", "click", "drag", "move", "key", "type", "stop"):
        p = sub.add_parser(name)
        p.add_argument("--out", required=True)
        if name == "shot":
            p.add_argument("name")
        elif name == "hmp":
            p.add_argument("command")
        elif name == "qom":
            p.add_argument("path")
            p.add_argument("property")
        elif name in ("click", "move"):
            p.add_argument("x", type=float)
            p.add_argument("y", type=float)
            p.add_argument("--size", default="1024x768")
            p.add_argument("--button", default="left")
            p.add_argument("--double", action="store_true")
            p.add_argument("--hold", type=float, default=0.12)
        elif name == "drag":
            for c in ("x0", "y0", "x1", "y1"):
                p.add_argument(c, type=float)
            p.add_argument("--size", default="1024x768")
        elif name == "key":
            p.add_argument("keys", nargs="+")
        elif name == "type":
            p.add_argument("text")
    a = ap.parse_args()
    if a.cmd == "start":
        cmd_start(a)
    elif a.cmd == "shot":
        print(shot(a.out, a.name))
    elif a.cmd == "hmp":
        print(hmp(a.out, a.command))
    elif a.cmd == "qom":
        q = QMP(a.out)
        print(q.cmd("qom-get", path=a.path, property=a.property))
        q.close()
    elif a.cmd == "click":
        cmd_click(a)
    elif a.cmd == "drag":
        cmd_drag(a)
    elif a.cmd == "move":
        cmd_move(a)
    elif a.cmd == "key":
        for k in a.keys:
            hmp(a.out, f"sendkey {k}")
            time.sleep(0.15)
    elif a.cmd == "type":
        for ch in a.text:
            key = {" ": "spc", "\n": "ret", ".": "dot", "-": "minus", "/": "slash"}.get(ch)
            if key is None:
                key = ch.lower() if not ch.isupper() else f"shift-{ch.lower()}"
            hmp(a.out, f"sendkey {key}")
            time.sleep(0.08)
    elif a.cmd == "stop":
        cmd_stop(a)


if __name__ == "__main__":
    main()
