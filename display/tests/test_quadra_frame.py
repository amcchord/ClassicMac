#!/usr/bin/env python3
"""Check Quadra shared-display pixels without an Apple ROM or guest disk.

Use qtest to program the built-in DAFB palette at 640x480 (the initial QEMU
placeholder size) and 800x600, then compare every shared-display pixel with
the independently captured QMP screen. Reconnect and palette changes must
also deliver the actual framebuffer. Usage: test_quadra_frame.py QEMU_M68K
"""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile

import cmdclient


def check(qemu, width, height):
    with tempfile.TemporaryDirectory(prefix="cmq-") as folder:
        root = Path(folder)
        display = cmdclient.DisplayClient(str(root / "d"))
        qmp = qt = stream = None
        with (root / "qemu.log").open("wb") as log:
            process = subprocess.Popen([
                qemu, "-M", "q800,audiodev=audio0", "-m", "128", "-g", f"{width}x{height}x8",
                "-accel", "qtest", "-S", "-qtest", f"unix:{root}/t,server=on,wait=off",
                "-qtest-log", os.devnull, "-qmp", f"unix:{root}/q,server=on,wait=off",
                "-display", f"classicmac,socket={root}/d", "-nic", "none",
                "-audiodev", "none,id=audio0",
                "-monitor", "none", "-serial", "none",
            ], stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                display.accept(10)
                qmp = cmdclient.QMP(str(root / "q"))
                qt = socket.socket(socket.AF_UNIX)
                qt.connect(str(root / "t"))
                qt.settimeout(10)
                stream = qt.makefile("rwb", buffering=0)

                def write(address, value):
                    stream.write(f"writeb {address:#x} {value:#x}\n".encode())
                    reply = stream.readline().strip()
                    assert reply == b"OK", reply

                def capture(rgb):
                    # VRAM is zero after reset: change entry zero in the DAFB CLUT.
                    write(0xf9800200, 0)
                    for component in rgb:
                        write(0xf9800210, component)
                    qmp.cmd("screendump", filename=str(root / "screen.ppm"))
                    data = (root / "screen.ppm").read_bytes()
                    header = f"P6\n{width} {height}\n255\n".encode()
                    assert data.startswith(header), data[:40]
                    expected = bytes(rgb) * (width * height)
                    assert data[len(header):] == expected, "QMP did not draw the programmed palette"

                    def matches():
                        s = display.surface
                        return s and (s["width"], s["height"]) == (width, height) and \
                            cmdclient.xrgb_to_rgb(display.frame, width, height, s["stride"]) == expected

                    assert display.wait_for(matches, 5), \
                        f"{width}x{height}: shared display differs from the real QMP framebuffer ({rgb})"
                    assert display.frame_matches_shared(), "DAMAGE left stale pixels in the client"

                capture((231, 73, 149))
                capture((19, 177, 82))
                display.drop()
                display.accept(5)
                capture((61, 92, 203))
                qmp.cmd("system_reset")
                capture((204, 119, 37))
                print(f"    OK  Quadra {width}x{height}: palette changes, reconnect, reset; "
                      f"{width * height * 4:,} exact RGB pixels")
            except Exception:
                log.flush()
                print((root / "qemu.log").read_text(errors="replace")[-2000:], file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                if qmp:
                    qmp.close()
                if stream:
                    stream.close()
                if qt:
                    qt.close()
                display.close()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    for size in ((640, 480), (800, 600)):
        check(os.path.abspath(sys.argv[1]), *size)
