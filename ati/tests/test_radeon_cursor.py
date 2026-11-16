#!/usr/bin/env python3
"""Check actual Radeon cursor pixels without a guest OS or private ROM.

Adapted from RetroBoot 88d074a research/gfx/tests/qtest_radeon_cursor.py.
Uses loopback TCP for qtest so the same gate runs on macOS, Linux and Windows.
See ati/README.md for provenance. Python standard library only.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import tempfile

from test_qemu_ati import QMP


class Card:
    def __init__(self, qemu, support):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen(1)
        self.listener.settimeout(20)
        port = self.listener.getsockname()[1]
        env = dict(os.environ, CLASSICMAC_ATI_3D="off")
        self.qmp = QMP([
            qemu, "-M", "mac99,via=cuda,audiodev=snd0", "-audiodev", "none,id=snd0",
            "-cpu", "7400", "-m", "256", "-L", support,
            "-display", "none", "-monitor", "none", "-serial", "none",
            "-nic", "none", "-S", "-qmp", "stdio",
            "-vga", "none", "-nodefaults", "-device",
            "ati-vga,model=r100,vgamem_mb=32,romfile=,addr=0x11,guest_hwcursor=on",
            "-qtest", f"tcp:127.0.0.1:{port}", "-qtest-log", os.devnull,
        ], env)
        self.socket, _ = self.listener.accept()
        self.socket.settimeout(20)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.stream = self.socket.makefile("rwb", buffering=0)

    def write(self, address, data):
        self.stream.write(f"write {address:#x} {len(data)} 0x{data.hex()}\n".encode())
        answer = self.stream.readline().strip()
        assert answer == b"OK", answer

    def word(self, address, value):
        self.write(address, struct.pack("<I", value))

    def config(self, register, value):
        self.word(0xf2800000, 0x80000000 | (0x11 << 11) | register)
        self.word(0xf2c00000, value)

    def reg(self, register, value):
        self.word(0x88000000 + register, value)

    def close(self):
        log = self.qmp.close()
        self.stream.close()
        self.socket.close()
        self.listener.close()
        return log


def run(qemu, support, output):
    card = Card(qemu, support)
    report = {"monochromeCases": 0, "argbModes": [], "pixelsCompared": 0}
    try:
        card.config(0x10, 0x84000000)
        card.config(0x18, 0x88000000)
        card.config(0x04, 7)
        for register, value in {
            0x148: 0x01ff0000, 0xe0: 2,
            0x200: (7 << 16) | 39, 0x208: (63 << 16) | 108,
            0x224: 0x10000, 0x228: 0, 0x22c: 8,
            0x260: 0x40000, 0x264: (5 << 16) | 5, 0x268: 0,
            0x54: 0x8000,
        }.items():
            card.reg(register, value)

        def screenshot(name):
            path = output / (name + ".ppm")
            card.qmp.cmd("screendump", filename=path.as_posix())
            magic, geometry, maximum, pixels = path.read_bytes().split(b"\n", 3)
            assert magic == b"P6" and maximum == b"255"
            assert tuple(map(int, geometry.split())) == (64, 64), geometry
            assert len(pixels) == 64 * 64 * 3
            return lambda x, y: pixels[(y * 64 + x) * 3:(y * 64 + x) * 3 + 3]

        def expect(pixel, x, y, wanted, context):
            actual = pixel(x, y)
            assert actual == wanted, (context, x, y, actual.hex(), wanted.hex())
            report["pixelsCompared"] += 1

        mono = bytearray()
        for y in range(64):
            a = x = 0
            for col in range(64):
                code = (col + 3 * y) % 4
                a = (a << 1) | (code >> 1)
                x = (x << 1) | (code & 1)
            mono += struct.pack(">QQ", a, x)
        card.write(0x84040000, mono)
        card.reg(0xe0, 0)
        for depth, packed, background in (
                (6, bytes.fromhex("ff000000"), bytes((0, 0, 255))),
                (4, bytes.fromhex("1f00"), bytes((0, 0, 248)))):
            card.reg(0x50, 0x03000000 | (depth << 8) | 0x10000)
            card.write(0x84010000, packed * (64 * 64))
            card.reg(0x26c, 0)
            card.reg(0x270, 0xffffff)
            for yorigin in (0, 1, 63):
                for xorigin in (0, 1, 7, 8, 31, 63):
                    for xpos, ypos in ((0, 0), (5, 5), (63, 63), (64, 5)):
                        card.reg(0x260, 0x40000 + yorigin * 16)
                        card.reg(0x268, (xorigin << 16) | yorigin)
                        card.reg(0x264, (xpos << 16) | ypos)
                        pixel = screenshot("monochrome")
                        context = ("monochrome", depth, xorigin, yorigin, xpos, ypos)
                        for sy in range(64):
                            for sx in range(64):
                                wanted = background
                                dx, dy = sx - xpos, sy - ypos
                                if 0 <= dx < 64 - xorigin and 0 <= dy < 64 - yorigin:
                                    code = (dx + xorigin + 3 * (dy + yorigin)) % 4
                                    if code == 0:
                                        wanted = bytes((0, 0, 0))
                                    elif code == 1:
                                        wanted = bytes((255, 255, 255))
                                    elif code == 3:
                                        wanted = bytes(c ^ 255 for c in background)
                                expect(pixel, sx, sy, wanted, context)
                        report["monochromeCases"] += 1
            card.reg(0x260, 0x40000)
            card.reg(0x268, 0)
            card.reg(0x264, (5 << 16) | 5)
            pixel = screenshot(f"before-disable-{depth}")
            expect(pixel, 5, 5, bytes((0, 0, 0)), ("visible before disable", depth))
            card.reg(0x50, 0x03000000 | (depth << 8))
            pixel = screenshot("disabled")
            # Pixman's unshadowed 16-bit surface expands blue to full range.
            for y in range(64):
                for x in range(64):
                    expect(pixel, x, y, bytes((0, 0, 255)), ("disabled", depth))
        print(f"PASS {report['monochromeCases']} monochrome edge/plane/depth cases and disable erasure")

        card.write(0x84010000, bytes.fromhex("ff0000ff") * (64 * 64))
        cursor = bytearray(64 * 64 * 4)
        cursor[:8] = bytes.fromhex("80ff0000ff00ff00")
        card.write(0x84040000, cursor)
        card.reg(0xe0, 2)
        card.reg(0x260, 0x40000)
        card.reg(0x264, (5 << 16) | 5)
        for mode in (2, 3):
            card.reg(0x268, 0)
            card.reg(0x50, 0x03000000 | (6 << 8) | (mode << 20) | 0x10000)
            pixel = screenshot(f"argb-{mode}")
            for x, y, color in ((0, 0, (0, 0, 255)), (5, 5, (128, 0, 127)),
                                (6, 5, (0, 255, 0)), (7, 5, (0, 0, 255))):
                expect(pixel, x, y, bytes(color), ("ARGB", mode))
            card.reg(0x268, 1 << 16)
            pixel = screenshot(f"argb-{mode}-left")
            expect(pixel, 5, 5, bytes((0, 255, 0)), ("ARGB left clip", mode))
            report["argbModes"].append(mode)
        card.reg(0x264, (63 << 16) | 5)
        card.reg(0x268, 0)
        pixel = screenshot("right-edge")
        expect(pixel, 63, 5, bytes((128, 0, 127)), "right clip")
        expect(pixel, 0, 6, bytes((0, 0, 255)), "no row wrap")
        print("PASS ARGB modes 2/3: transparency, alpha, left and right clipping")
        report.update(result="passed", differingPixels=0, cursorDisableErasure=True)
        return report
    finally:
        (output / "qemu.log").write_text(card.close())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--support", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cm-radeon-cursor-") as temporary:
        output = (args.output or Path(temporary)).resolve()
        if args.output and output.exists() and any(output.iterdir()):
            parser.error("Choose a new empty output folder.")
        output.mkdir(parents=True, exist_ok=True)
        report = run(str(args.qemu.resolve()), str(args.support.resolve()), output)
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"RESULT ok: {report['pixelsCompared']:,} exact RGB pixels")


if __name__ == "__main__":
    main()
