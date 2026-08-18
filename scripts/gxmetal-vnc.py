#!/usr/bin/env python3
"""Capture and control a local QEMU VNC display without external packages."""

import argparse
import socket
import struct
import time
import zlib


KEYSYMS = {
    "BackSpace": 0xFF08,
    "Tab": 0xFF09,
    "Return": 0xFF0D,
    "Escape": 0xFF1B,
    "Home": 0xFF50,
    "Left": 0xFF51,
    "Up": 0xFF52,
    "Right": 0xFF53,
    "Down": 0xFF54,
    "PageUp": 0xFF55,
    "PageDown": 0xFF56,
    "End": 0xFF57,
    "Insert": 0xFF63,
    "Delete": 0xFFFF,
    "KP_Enter": 0xFF8D,
    "Shift_L": 0xFFE1,
    "Control_L": 0xFFE3,
    "Meta_L": 0xFFE7,
    "Alt_L": 0xFFE9,
    "Super_L": 0xFFEB,
    "Space": 0x20,
}


def receive_exact(connection, length):
    chunks = []
    received = 0
    while received < length:
        chunk = connection.recv(length - received)
        if not chunk:
            raise RuntimeError("VNC server closed the connection")
        chunks.append(chunk)
        received += len(chunk)
    return b"".join(chunks)


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path, width, height, rgb):
    scanlines = bytearray()
    row_bytes = width * 3
    for y in range(height):
        scanlines.append(0)
        start = y * row_bytes
        scanlines.extend(rgb[start:start + row_bytes])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as output:
        output.write(b"\x89PNG\r\n\x1a\n")
        output.write(png_chunk(b"IHDR", header))
        output.write(png_chunk(b"IDAT", zlib.compress(scanlines, 6)))
        output.write(png_chunk(b"IEND", b""))


class RFBClient:
    def __init__(self, connection):
        self.connection = connection
        self.width = 0
        self.height = 0

    def connect(self):
        version = receive_exact(self.connection, 12)
        if not version.startswith(b"RFB 003."):
            raise RuntimeError("invalid VNC protocol banner")
        server_minor = int(version[8:11])
        client_minor = 8 if server_minor >= 8 else 3
        self.connection.sendall(
            ("RFB 003.%03d\n" % client_minor).encode("ascii"))
        if client_minor == 3:
            security = struct.unpack(">I", receive_exact(
                self.connection, 4))[0]
            if security != 1:
                raise RuntimeError("VNC server requires authentication")
        else:
            count = receive_exact(self.connection, 1)[0]
            if count == 0:
                length = struct.unpack(">I", receive_exact(
                    self.connection, 4))[0]
                reason = receive_exact(self.connection, length)
                raise RuntimeError(reason.decode("utf-8", "replace"))
            security_types = receive_exact(self.connection, count)
            if 1 not in security_types:
                raise RuntimeError("VNC server does not allow local no-auth access")
            self.connection.sendall(b"\x01")
            result = struct.unpack(">I", receive_exact(
                self.connection, 4))[0]
            if result != 0:
                raise RuntimeError("VNC security negotiation failed")

        self.connection.sendall(b"\x01")
        self.width, self.height = struct.unpack(">HH", receive_exact(
            self.connection, 4))
        receive_exact(self.connection, 16)
        name_length = struct.unpack(">I", receive_exact(
            self.connection, 4))[0]
        receive_exact(self.connection, name_length)

        # Request 32-bit little-endian true color (B, G, R, padding in memory)
        # and raw rectangles only. This keeps the client deterministic and
        # avoids pulling a VNC library into the release toolchain.
        pixel_format = struct.pack(
            ">BBBBHHHBBBxxx", 32, 24, 0, 1,
            255, 255, 255, 16, 8, 0)
        self.connection.sendall(b"\x00\x00\x00\x00" + pixel_format)
        self.connection.sendall(struct.pack(">BBHi", 2, 0, 1, 0))

    def keysym(self, name):
        if name in KEYSYMS:
            return KEYSYMS[name]
        elif len(name) == 1:
            return ord(name)
        raise ValueError("unknown VNC key: %s" % name)

    def key_event(self, keysym, down):
        self.connection.sendall(struct.pack(">BBHI", 4, down, 0, keysym))

    def key(self, name, hold_seconds):
        keysym = self.keysym(name)
        self.key_event(keysym, 1)
        time.sleep(hold_seconds)
        self.key_event(keysym, 0)

    def chord(self, value, hold_seconds):
        names = value.split("+")
        if len(names) < 2 or any(not name for name in names):
            raise ValueError("chord must contain two or more '+'-separated keys")
        keysyms = [self.keysym(name) for name in names]
        for keysym in keysyms:
            self.key_event(keysym, 1)
        time.sleep(hold_seconds)
        for keysym in reversed(keysyms):
            self.key_event(keysym, 0)

    def click(self, x, y):
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            raise ValueError("pointer coordinate outside VNC framebuffer")
        # QEMU's relative ADB mouse needs a button-up motion event before a
        # button transition from a newly connected VNC client. Without it,
        # the first requested coordinate only synchronizes QEMU's pointer and
        # the click lands at the previous guest position.
        self.connection.sendall(struct.pack(">BBHH", 5, 0, x, y))
        time.sleep(0.05)
        self.connection.sendall(struct.pack(">BBHH", 5, 1, x, y))
        time.sleep(0.05)
        self.connection.sendall(struct.pack(">BBHH", 5, 0, x, y))

    def capture(self):
        self.connection.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0,
                                            self.width, self.height))
        rgb = bytearray(self.width * self.height * 3)
        while True:
            message_type = receive_exact(self.connection, 1)[0]
            if message_type == 0:
                receive_exact(self.connection, 1)
                rectangles = struct.unpack(">H", receive_exact(
                    self.connection, 2))[0]
                for _ in range(rectangles):
                    x, y, width, height, encoding = struct.unpack(
                        ">HHHHi", receive_exact(self.connection, 12))
                    if encoding != 0:
                        raise RuntimeError(
                            "unexpected VNC encoding %d" % encoding)
                    raw = receive_exact(self.connection, width * height * 4)
                    for row in range(height):
                        source = row * width * 4
                        destination = ((y + row) * self.width + x) * 3
                        for column in range(width):
                            offset = source + column * 4
                            rgb[destination:destination + 3] = (
                                raw[offset + 2], raw[offset + 1], raw[offset])
                            destination += 3
                return rgb
            if message_type == 2:  # Bell
                continue
            if message_type == 3:  # ServerCutText
                receive_exact(self.connection, 3)
                length = struct.unpack(">I", receive_exact(
                    self.connection, 4))[0]
                receive_exact(self.connection, length)
                continue
            raise RuntimeError("unexpected VNC server message %d" %
                               message_type)


def parse_click(value):
    try:
        x, y = value.split(",", 1)
        return int(x), int(y)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "click must be formatted as X,Y") from error


def main():
    parser = argparse.ArgumentParser()
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument("--unix-socket")
    endpoint.add_argument("--tcp", metavar="HOST:PORT")
    parser.add_argument("--key", action="append", default=[])
    parser.add_argument("--chord", action="append", default=[])
    parser.add_argument("--click", action="append", type=parse_click,
                        default=[])
    parser.add_argument("--hold-ms", type=int, default=150)
    parser.add_argument("--delay", type=float, default=0.0)
    parser.add_argument("--screenshot")
    args = parser.parse_args()

    if args.unix_socket:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connection.connect(args.unix_socket)
    else:
        try:
            host, port = args.tcp.rsplit(":", 1)
            address = (host, int(port))
        except ValueError as error:
            raise SystemExit("--tcp must be formatted as HOST:PORT") from error
        connection = socket.create_connection(address)

    try:
        client = RFBClient(connection)
        client.connect()
        for chord in args.chord:
            client.chord(chord, args.hold_ms / 1000.0)
        for key in args.key:
            client.key(key, args.hold_ms / 1000.0)
        for x, y in args.click:
            client.click(x, y)
        if args.delay > 0:
            time.sleep(args.delay)
        if args.screenshot:
            write_png(args.screenshot, client.width, client.height,
                      client.capture())
            print("%s: %dx%d" % (args.screenshot,
                                  client.width, client.height))
    finally:
        connection.close()


if __name__ == "__main__":
    main()
