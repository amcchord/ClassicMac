"""Test client for QEMU's ClassicMac display backend (-display classicmac).

Python standard library only. It plays the app's side of the protocol in
display/README.md: listen on an AF_UNIX stream socket, accept QEMU, receive
HELLO / SURFACE (with the shared buffer's descriptor) / DAMAGE /
GUEST_EVENT / CURSOR / MOUSE, keep a private copy of the frame that is
updated only from DAMAGE rectangles (as the app's texture would be), and
send KEY / POINTER_ABS / BUTTON / RESIZE / RELEASE_ALL / REFRESH_RATE /
POINTER_REL.
"""
import array
import json
import mmap
import os
import select
import socket
import struct
import time
import zlib

HELLO, SURFACE, DAMAGE, GUEST_EVENT, CURSOR, MOUSE = 1, 2, 3, 4, 5, 6
KEY, POINTER_ABS, BUTTON, RESIZE, RELEASE_ALL, REFRESH_RATE, POINTER_REL = \
    16, 17, 18, 19, 20, 21, 22
XRGB8888 = 0x34325258
NAMES = {HELLO: "HELLO", SURFACE: "SURFACE", DAMAGE: "DAMAGE",
         GUEST_EVENT: "GUEST_EVENT", CURSOR: "CURSOR", MOUSE: "MOUSE"}

# QAPI QKeyCode values (qapi/ui.json order) used by the tests.
QCODE = {name: i for i, name in enumerate((
    "unmapped", "shift", "shift_r", "alt", "alt_r", "ctrl", "ctrl_r", "menu",
    "esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "minus", "equal",
    "backspace", "tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    "bracket_left", "bracket_right", "ret", "a", "s", "d", "f", "g", "h", "j",
    "k", "l", "semicolon", "apostrophe", "grave_accent", "backslash", "z", "x",
    "c", "v", "b", "n", "m", "comma", "dot", "slash", "asterisk", "spc",
    "caps_lock", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10",
    "num_lock", "scroll_lock", "kp_divide", "kp_multiply", "kp_subtract",
    "kp_add", "kp_enter", "kp_decimal", "sysrq", "kp_0", "kp_1", "kp_2",
    "kp_3", "kp_4", "kp_5", "kp_6", "kp_7", "kp_8", "kp_9", "less", "f11",
    "f12", "print", "home", "pgup", "pgdn", "end", "left", "up", "down",
    "right", "insert", "delete", "stop", "again", "props", "undo", "front",
    "copy", "open", "paste", "find", "cut", "lf", "help", "meta_l", "meta_r",
))}


def write_png(path, width, height, rgb):
    """Write 8-bit RGB pixels (bytes, row-major) as a PNG."""
    stride = width * 3
    raw = b"".join(b"\0" + rgb[y * stride:(y + 1) * stride]
                   for y in range(height))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2,
                                           0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def xrgb_to_rgb(buf, width, height, stride):
    """B,G,R,X bytes per pixel (XRGB8888 little-endian) to R,G,B bytes."""
    rows = bytes(buf) if stride == width * 4 else b"".join(
        bytes(buf[y * stride:y * stride + width * 4]) for y in range(height))
    rows = rows[:width * height * 4]
    out = bytearray(width * height * 3)
    out[0::3] = rows[2::4]
    out[1::3] = rows[1::4]
    out[2::3] = rows[0::4]
    return bytes(out)


class DisplayClient:
    def __init__(self, path):
        self.path = path
        if os.path.exists(path):
            os.unlink(path)
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.listener.bind(path)
        self.listener.listen(1)
        self.conn = None
        self.inbuf = bytearray()
        self.fds = []               # descriptors not yet claimed by a SURFACE
        self.hello = None
        self.surface = None         # dict(width, height, stride, format, size)
        self.mem = None             # mmap of the shared buffer
        self.frame = None           # private copy, updated from DAMAGE only
        self.surfaces = []          # (time, width, height, stride, size)
        self.damage = []            # (time, seq, [(x, y, w, h)...])
        self.events = []            # (time, dict)
        self.cursors = []           # (time, hot_x, hot_y, w, h, pixels)
        self.mice = []              # (time, x, y, visible)
        self.counts = {}
        self.last_seq = 0
        self.seq_gaps = 0
        self.connected_at = None

    # connection -----------------------------------------------------------
    def accept(self, timeout):
        self.listener.settimeout(timeout)
        self.conn, _ = self.listener.accept()
        self.conn.setblocking(False)
        self.connected_at = time.time()
        self.inbuf.clear()

    def drop(self):
        """Close the app side, as an app restart would."""
        if self.conn:
            self.conn.close()
            self.conn = None
        for fd in self.fds:
            os.close(fd)
        self.fds = []

    def close(self):
        self.drop()
        if self.mem is not None:
            self.mem.close()
            self.mem = None
        self.listener.close()
        try:
            os.unlink(self.path)
        except FileNotFoundError:
            pass

    # receiving ------------------------------------------------------------
    def pump(self, timeout=0.0):
        """Receive and handle everything that arrives within timeout."""
        end = time.time() + timeout
        while self.conn is not None:
            left = max(0.0, end - time.time())
            r, _, _ = select.select([self.conn], [], [], left)
            if not r:
                if time.time() >= end:
                    return
                continue
            try:
                data, anc, flags, _ = self.conn.recvmsg(
                    1 << 20, socket.CMSG_SPACE(16 * 4))
            except BlockingIOError:
                continue
            for level, kind, cdata in anc:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    fds = array.array("i")
                    fds.frombytes(cdata[:len(cdata) - len(cdata) % 4])
                    self.fds.extend(fds)
            if flags & getattr(socket, "MSG_CTRUNC", 0):
                raise RuntimeError("control data truncated")
            if not data:
                raise ConnectionError("QEMU closed the display socket")
            self.inbuf += data
            self._parse()
            if time.time() >= end and timeout > 0:
                return

    def _parse(self):
        off = 0
        while len(self.inbuf) - off >= 8:
            mtype, mlen = struct.unpack_from("=II", self.inbuf, off)
            if len(self.inbuf) - off < 8 + mlen:
                break
            payload = bytes(self.inbuf[off + 8:off + 8 + mlen])
            off += 8 + mlen
            self._handle(mtype, payload)
        del self.inbuf[:off]

    def _handle(self, mtype, p):
        now = time.time()
        self.counts[mtype] = self.counts.get(mtype, 0) + 1
        if mtype == HELLO:
            version, pid = struct.unpack_from("=II", p)
            session = p[8:48].split(b"\0", 1)[0].decode()
            self.hello = {"version": version, "pid": pid, "session": session,
                          "len": len(p)}
        elif mtype == SURFACE:
            w, h, stride, fmt, size = struct.unpack_from("=IIIIQ", p)
            if not self.fds:
                raise RuntimeError("SURFACE without a descriptor")
            fd = self.fds.pop(0)
            if self.mem is not None:
                self.mem.close()
            st = os.fstat(fd)
            self.mem = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ)
            os.close(fd)
            self.surface = {"width": w, "height": h, "stride": stride,
                            "format": fmt, "size": size,
                            "fstat_size": st.st_size}
            self.frame = bytearray(self.mem[:stride * h])
            self.surfaces.append((now, w, h, stride, size))
        elif mtype == DAMAGE:
            seq, count = struct.unpack_from("=II", p)
            rects = [struct.unpack_from("=iiii", p, 8 + 16 * i)
                     for i in range(count)]
            if self.last_seq and seq != self.last_seq + 1:
                self.seq_gaps += 1
            self.last_seq = seq
            self.damage.append((now, seq, rects))
            self._apply(rects)
        elif mtype == GUEST_EVENT:
            self.events.append((now, json.loads(p.decode())))
        elif mtype == CURSOR:
            hx, hy, w, h = struct.unpack_from("=iiII", p)
            pixels = array.array("I")
            pixels.frombytes(p[16:16 + 4 * w * h])
            self.cursors.append((now, hx, hy, w, h, pixels))
        elif mtype == MOUSE:
            x, y, vis = struct.unpack_from("=iiI", p)
            self.mice.append((now, x, y, vis))

    def _apply(self, rects):
        s = self.surface
        if not s:
            return
        stride, mem, frame = s["stride"], self.mem, self.frame
        for x, y, w, h in rects:
            for row in range(y, y + h):
                a = row * stride + x * 4
                frame[a:a + w * 4] = mem[a:a + w * 4]

    # sending --------------------------------------------------------------
    def send(self, mtype, payload=b""):
        msg = struct.pack("=II", mtype, len(payload)) + payload
        self.conn.setblocking(True)
        try:
            self.conn.sendall(msg)
        finally:
            self.conn.setblocking(False)

    def key(self, name, down):
        self.send(KEY, struct.pack("=II", QCODE[name], 1 if down else 0))

    def tap(self, *names, hold=0.08):
        for n in names:
            self.key(n, True)
        self.pump(hold)
        for n in reversed(names):
            self.key(n, False)
        self.pump(0.05)

    def pointer(self, x, y):
        self.send(POINTER_ABS, struct.pack("=II", int(x), int(y)))

    def pointer_rel(self, dx, dy):
        self.send(POINTER_REL, struct.pack("=ii", int(dx), int(dy)))

    def button(self, b, down):
        self.send(BUTTON, struct.pack("=II", b, 1 if down else 0))

    def click(self, x, y, hold=0.12):
        self.pointer(x, y)
        self.pump(0.15)
        self.button(0, True)
        self.pump(hold)
        self.button(0, False)
        self.pump(0.1)

    def resize(self, w, h):
        self.send(RESIZE, struct.pack("=II", w, h))

    def release_all(self):
        self.send(RELEASE_ALL)

    def refresh_rate(self, ms):
        self.send(REFRESH_RATE, struct.pack("=I", ms))

    # frames ---------------------------------------------------------------
    def snapshot(self, path, source="frame"):
        """Save the private (damage-applied) copy, or the shared buffer."""
        s = self.surface
        buf = self.frame if source == "frame" else self.mem
        rgb = xrgb_to_rgb(buf, s["width"], s["height"], s["stride"])
        write_png(path, s["width"], s["height"], rgb)
        return path

    def frame_matches_shared(self):
        """Does the damage-applied copy equal the shared buffer?"""
        s = self.surface
        n = s["stride"] * s["height"]
        if bytes(self.frame[:n]) == self.mem[:n]:
            return True, 0
        diff = 0
        stride = s["stride"]
        for y in range(s["height"]):
            a = y * stride
            if self.frame[a:a + s["width"] * 4] != self.mem[a:a + s["width"] * 4]:
                diff += 1
        return False, diff

    def pixel(self, x, y, source="frame"):
        """(r, g, b) at x, y."""
        s = self.surface
        buf = self.frame if source == "frame" else self.mem
        a = y * s["stride"] + x * 4
        b, g, r = buf[a], buf[a + 1], buf[a + 2]
        return r, g, b

    def region_stats(self, x, y, w, h, source="frame"):
        """Mean (r, g, b) over a rectangle."""
        s = self.surface
        buf = self.frame if source == "frame" else self.mem
        tot = [0, 0, 0]
        n = 0
        for row in range(y, y + h, 2):
            a = row * s["stride"] + x * 4
            seg = bytes(buf[a:a + w * 4])
            tot[0] += sum(seg[2::8])
            tot[1] += sum(seg[1::8])
            tot[2] += sum(seg[0::8])
            n += len(seg[0::8])
        return tuple(t / max(n, 1) for t in tot)

    def wait_for(self, predicate, timeout, step=0.1):
        end = time.time() + timeout
        while time.time() < end:
            self.pump(step)
            if predicate():
                return True
        return predicate()


class QMP:
    def __init__(self, path, timeout=30):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.settimeout(timeout)
        self.s.connect(path)
        self.f = self.s.makefile("rw")
        json.loads(self.f.readline())
        self.events = []
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            msg = json.loads(self.f.readline())
            if "event" in msg:
                self.events.append(msg)
                continue
            if "error" in msg:
                raise RuntimeError(f"{name}: {msg['error']}")
            return msg.get("return")

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})

    def close(self):
        self.f.close()
        self.s.close()
