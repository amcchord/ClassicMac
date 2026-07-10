"""Minimal QMP client + helpers for driving the mac99 OS 9 install harness.

Talks to a running qemu-system-ppc over a QMP unix socket. Provides just what
the trial runner needs: capability handshake, command execution, absolute
pointer clicks via input-send-event (works because the guest uses the virtio
tablet / absolute pointer), screen capture, and disk-write progress sampling
via the trace log.
"""

import json
import os
import socket
import subprocess
import time


class QMP:
    def __init__(self, sock_path):
        self.sock_path = sock_path
        self.sock = None
        self.f = None

    def connect(self, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(self.sock_path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.2)
        else:
            raise TimeoutError(f"QMP socket {self.sock_path} never appeared")
        self.f = self.sock.makefile("rw", encoding="utf-8", newline="\n")
        # Greeting banner.
        self._readline()
        self.execute("qmp_capabilities")

    def _readline(self):
        while True:
            line = self.f.readline()
            if not line:
                raise ConnectionError("QMP connection closed")
            obj = json.loads(line)
            # Skip async events; callers only want command replies.
            if "event" in obj:
                continue
            return obj

    def execute(self, cmd, **args):
        req = {"execute": cmd}
        if args:
            req["arguments"] = args
        self.f.write(json.dumps(req) + "\n")
        self.f.flush()
        reply = self._readline()
        if "error" in reply:
            raise RuntimeError(f"QMP {cmd} error: {reply['error']}")
        return reply.get("return")

    def hmp(self, command_line):
        return self.execute("human-monitor-command", **{"command-line": command_line})

    # Absolute pointer: input-send-event with abs axes scaled 0..32767.
    def move_abs(self, x, y, width, height):
        ax = int(x * 32767 / (width - 1))
        ay = int(y * 32767 / (height - 1))
        self.execute("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": ax}},
            {"type": "abs", "data": {"axis": "y", "value": ay}},
        ])

    def click_abs(self, x, y, width, height, double=False):
        self.move_abs(x, y, width, height)
        time.sleep(0.15)
        for _ in range(2 if double else 1):
            self.execute("input-send-event", events=[
                {"type": "btn", "data": {"button": "left", "down": True}}])
            time.sleep(0.05)
            self.execute("input-send-event", events=[
                {"type": "btn", "data": {"button": "left", "down": False}}])
            time.sleep(0.08)

    def key(self, *qcodes):
        self.execute("input-send-event", events=[
            {"type": "key", "data": {"down": True,
             "key": {"type": "qcode", "data": q}}} for q in qcodes] + [
            {"type": "key", "data": {"down": False,
             "key": {"type": "qcode", "data": q}}} for q in reversed(qcodes)])

    def screendump(self, out_ppm):
        # QMP screendump writes a PPM; convert to PNG for inspection.
        self.execute("screendump", filename=out_ppm)
        png = out_ppm.rsplit(".", 1)[0] + ".png"
        subprocess.run(["sips", "-s", "format", "png", out_ppm, "--out", png],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return png

    def status(self):
        return self.execute("query-status").get("status")

    def quit(self):
        try:
            self.execute("quit")
        except Exception:
            pass


def trace_count(trace_path, needle):
    """Count occurrences of a substring in the trace file (cheap stall probe)."""
    if not os.path.exists(trace_path):
        return 0
    n = 0
    with open(trace_path, "rb") as fh:
        needle_b = needle.encode()
        for chunk in iter(lambda: fh.readline(), b""):
            if needle_b in chunk:
                n += 1
    return n
