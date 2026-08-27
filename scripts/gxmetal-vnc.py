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
    "F1": 0xFFBE,
    "F2": 0xFFBF,
    "F3": 0xFFC0,
    "F4": 0xFFC1,
    "F5": 0xFFC2,
    "F6": 0xFFC3,
    "F7": 0xFFC4,
    "F8": 0xFFC5,
    "F9": 0xFFC6,
    "F10": 0xFFC7,
    "F11": 0xFFC8,
    "F12": 0xFFC9,
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
        self.pointer_x = None
        self.pointer_y = None

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
        # and raw rectangles. DesktopSize notifications keep a connection
        # valid when Open Firmware and Mac OS use different resolutions.
        # This keeps the client deterministic and avoids pulling a VNC
        # library into the release toolchain.
        pixel_format = struct.pack(
            ">BBBBHHHBBBxxx", 32, 24, 0, 1,
            255, 255, 255, 16, 8, 0)
        self.connection.sendall(b"\x00\x00\x00\x00" + pixel_format)
        self.connection.sendall(struct.pack(">BBHiii", 2, 0, 3,
                                            0, -223, -308))

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

    def pointer_event(self, x, y, buttons=0):
        self.connection.sendall(struct.pack(">BBHH", 5, buttons, x, y))

    def move_to(self, x, y):
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            raise ValueError("pointer coordinate outside VNC framebuffer")
        # RFB pointer messages carry absolute coordinates, but QEMU converts
        # them to deltas for the Power Mac's relative USB/ADB mouse. The first
        # event on a connection only establishes QEMU's reference position.
        # Establish it at the lower-right, send one large negative motion to
        # home the clipped guest cursor, then use paced one-pixel deltas so
        # classic Mac OS mouse acceleration cannot overshoot the target.
        if self.pointer_x is None:
            self.pointer_event(self.width - 1, self.height - 1)
            time.sleep(0.05)
            self.pointer_event(0, 0)
            time.sleep(0.05)
            start_x = 0
            start_y = 0
        else:
            start_x = self.pointer_x
            start_y = self.pointer_y
        delta_x = x - start_x
        delta_y = y - start_y
        distance = max(abs(delta_x), abs(delta_y))
        sign_x = 1 if delta_x > 0 else (-1 if delta_x < 0 else 0)
        sign_y = 1 if delta_y > 0 else (-1 if delta_y < 0 else 0)
        for step in range(1, distance + 1):
            pointer_x = start_x + sign_x * min(step, abs(delta_x))
            pointer_y = start_y + sign_y * min(step, abs(delta_y))
            self.pointer_event(pointer_x, pointer_y)
            time.sleep(0.002)
        self.pointer_x = x
        self.pointer_y = y

    def click_buttons(self, x, y, count):
        for index in range(count):
            self.pointer_event(x, y, 1)
            time.sleep(0.05)
            self.pointer_event(x, y)
            if index + 1 < count:
                time.sleep(0.12)

    def click(self, x, y):
        self.move_to(x, y)
        self.click_buttons(x, y, 1)

    def hold_click(self, x, y, hold_seconds):
        self.move_to(x, y)
        self.pointer_event(x, y, 1)
        try:
            time.sleep(hold_seconds)
        finally:
            self.pointer_event(x, y)

    def double_click(self, x, y):
        self.move_to(x, y)
        self.click_buttons(x, y, 2)

    def drag(self, start_x, start_y, end_x, end_y):
        self.move_to(start_x, start_y)
        self.pointer_event(start_x, start_y, 1)
        delta_x = end_x - start_x
        delta_y = end_y - start_y
        distance = max(abs(delta_x), abs(delta_y))
        for step in range(1, distance + 1):
            fraction = step / distance
            x = round(start_x + delta_x * fraction)
            y = round(start_y + delta_y * fraction)
            self.pointer_event(x, y, 1)
            time.sleep(0.002)
        self.pointer_event(end_x, end_y)
        self.pointer_x = end_x
        self.pointer_y = end_y

    def capture(self):
        def request_full_update():
            self.connection.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0,
                                                self.width, self.height))

        request_full_update()
        rgb = bytearray(self.width * self.height * 3)
        while True:
            message_type = receive_exact(self.connection, 1)[0]
            if message_type == 0:
                receive_exact(self.connection, 1)
                rectangles = struct.unpack(">H", receive_exact(
                    self.connection, 2))[0]
                resized = False
                saw_pixels = False
                for _ in range(rectangles):
                    x, y, width, height, encoding = struct.unpack(
                        ">HHHHi", receive_exact(self.connection, 12))
                    if encoding in (-223, -308):
                        if encoding == -308:
                            screen_count = receive_exact(
                                self.connection, 1)[0]
                            receive_exact(self.connection, 3 +
                                          screen_count * 16)
                        if width < 1 or height < 1:
                            raise RuntimeError(
                                "invalid VNC desktop size %dx%d" %
                                (width, height))
                        self.width = width
                        self.height = height
                        self.pointer_x = None
                        self.pointer_y = None
                        rgb = bytearray(width * height * 3)
                        resized = True
                        continue
                    if encoding != 0:
                        raise RuntimeError(
                            "unexpected VNC encoding %d" % encoding)
                    if (x + width > self.width or
                            y + height > self.height):
                        raise RuntimeError(
                            "VNC rectangle outside framebuffer")
                    raw = receive_exact(self.connection, width * height * 4)
                    for row in range(height):
                        source = row * width * 4
                        destination = ((y + row) * self.width + x) * 3
                        for column in range(width):
                            offset = source + column * 4
                            rgb[destination:destination + 3] = (
                                raw[offset + 2], raw[offset + 1], raw[offset])
                            destination += 3
                    saw_pixels = True
                if resized:
                    # A resize rectangle may be the only rectangle in this
                    # update. Request a complete frame at the new dimensions;
                    # any pixels bundled with the resize are intentionally
                    # discarded so callers always receive one coherent frame.
                    rgb = bytearray(self.width * self.height * 3)
                    request_full_update()
                    continue
                if saw_pixels:
                    return rgb
                request_full_update()
                continue
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


def parse_pixel(value):
    try:
        fields = [int(field) for field in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "pixel must contain integer fields") from error
    if len(fields) not in (5, 6):
        raise argparse.ArgumentTypeError(
            "pixel must be formatted as X,Y,R,G,B[,TOLERANCE]")
    x, y, red, green, blue = fields[:5]
    tolerance = fields[5] if len(fields) == 6 else 8
    if x < 0 or y < 0:
        raise argparse.ArgumentTypeError(
            "pixel coordinates must be nonnegative")
    if any(channel < 0 or channel > 255
           for channel in (red, green, blue, tolerance)):
        raise argparse.ArgumentTypeError(
            "pixel channels and tolerance must be from 0 through 255")
    return x, y, red, green, blue, tolerance


def wait_for_pixel(client, settings, timeout, poll_interval):
    x, y, red, green, blue, tolerance = settings
    target = (red, green, blue)
    deadline = time.monotonic() + timeout
    while True:
        rgb = client.capture()
        if x >= client.width or y >= client.height:
            raise RuntimeError(
                "pixel coordinate (%d, %d) is outside %dx%d framebuffer" %
                (x, y, client.width, client.height))
        offset = (y * client.width + x) * 3
        actual = tuple(rgb[offset:offset + 3])
        if all(abs(actual[channel] - target[channel]) <= tolerance
               for channel in range(3)):
            return actual
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(
                "timed out waiting for pixel (%d, %d) to match %s "
                "within tolerance %d; last value was %s" %
                (x, y, target, tolerance, actual))
        time.sleep(min(poll_interval, remaining))


def main():
    parser = argparse.ArgumentParser()
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument("--unix-socket")
    endpoint.add_argument("--tcp", metavar="HOST:PORT")
    parser.add_argument("--key", action="append", default=[])
    parser.add_argument("--chord", action="append", default=[])
    parser.add_argument("--click", action="append", type=parse_click,
                        default=[])
    parser.add_argument("--hold-click", action="append", type=parse_click,
                        default=[])
    parser.add_argument("--double-click", action="append", type=parse_click,
                        default=[])
    parser.add_argument("--wait-for-pixel", type=parse_pixel)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--poll-interval", type=float, default=1.0)
    parser.add_argument("--hold-ms", type=int, default=150)
    parser.add_argument("--delay", type=float, default=0.0)
    parser.add_argument("--screenshot")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.poll_interval <= 0:
        parser.error("--poll-interval must be positive")

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
        if args.wait_for_pixel:
            actual = wait_for_pixel(
                client, args.wait_for_pixel, args.timeout,
                args.poll_interval)
            print("pixel detected: (%d, %d) = %s" %
                  (args.wait_for_pixel[0], args.wait_for_pixel[1], actual))
        for chord in args.chord:
            client.chord(chord, args.hold_ms / 1000.0)
        for key in args.key:
            client.key(key, args.hold_ms / 1000.0)
        for x, y in args.click:
            client.click(x, y)
        for x, y in args.hold_click:
            client.hold_click(x, y, args.hold_ms / 1000.0)
        for x, y in args.double_click:
            client.double_click(x, y)
        if args.delay > 0:
            time.sleep(args.delay)
        if args.screenshot:
            rgb = client.capture()
            write_png(args.screenshot, client.width, client.height, rgb)
            print("%s: %dx%d" % (args.screenshot,
                                  client.width, client.height))
    finally:
        connection.close()


if __name__ == "__main__":
    main()
