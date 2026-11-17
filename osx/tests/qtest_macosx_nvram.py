#!/usr/bin/env python3
"""Exercise mac99's classic/OS X NVRAM layouts without guest software.

The OS X switch restores QEMU's two 4 KiB banks.  Check headers, lengths,
checksums and persistent writes against the actual device, rather than a
second implementation of its formatter.  No Apple ROM or disk is needed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import zlib


class NVRAM:
    def __init__(self, qemu, firmware, run, osx=False, backing=None, flash=False):
        self.flash = flash
        self.run = Path(run)
        self.log_path = self.run / "qemu.log"
        self.log = self.log_path.open("w")
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        argv = [str(qemu), "-M", "mac99,via=pmu,audiodev=snd0", "-S",
                "-audiodev", "none,id=snd0", "-display", "none",
                "-serial", "none", "-monitor", "none", "-nodefaults",
                "-L", str(firmware), "-prom-env", "output-device=ttya",
                "-bios", str(Path(firmware) / "openbios-ppc-osx"),
                "-qtest", f"tcp:127.0.0.1:{listener.getsockname()[1]}",
                "-qtest-log", os.devnull]
        if osx:
            argv += ["-global", "macio-nvram.osx-partitions=on"]
        if flash:
            argv += ["-global", "macio-nvram.osx-flash=on"]
        if backing is not None:
            argv += ["-drive", f"if=none,id=nv,format=raw,file={backing}",
                     "-global", "macio-nvram.drive=nv"]
        self.proc = subprocess.Popen(argv, stdout=self.log,
                                     stderr=subprocess.STDOUT)
        try:
            self.sock, _ = listener.accept()
        except BaseException as error:
            self.close()
            raise RuntimeError(self.log_path.read_text(errors="replace")) from error
        finally:
            listener.close()
        self.sock.settimeout(20)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.stream = self.sock.makefile("rw")

    def command(self, text):
        self.stream.write(text + "\n")
        self.stream.flush()
        while True:
            try:
                line = self.stream.readline()
            except OSError as error:
                self.close()
                raise RuntimeError(self.log_path.read_text(errors="replace")) from error
            if not line:
                self.close()
                raise RuntimeError("QEMU closed qtest: " + self.log_path.read_text(errors="replace"))
            if line.startswith("OK"):
                return line.split()
            if line.startswith(("FAIL", "ERR")):
                raise RuntimeError(line)

    def read(self):
        # mac99 maps each byte twice (it_shift=1).
        base = 0xffe00000 if self.flash else 0xfff04000
        reply = self.command(f"read {base:#x} 16384")
        data = bytes.fromhex(reply[1][2:])
        return data if self.flash else data[::2]

    def write_byte(self, offset, value):
        stride = 1 if self.flash else 2
        base = 0xffe00000 if self.flash else 0xfff04000
        self.command(f"writeb {base + stride * offset:#x} {value:#x}")

    def close(self):
        if hasattr(self, "stream"):
            self.stream.close()
        if hasattr(self, "sock"):
            self.sock.close()
        if hasattr(self, "proc") and self.proc.poll() is None:
            self.proc.terminate()
            self.proc.wait(timeout=10)
        self.log.close()


def partitions(data, end):
    result = []
    offset = 0
    while offset < end:
        header = data[offset:offset + 16]
        assert len(header) == 16
        signature, checksum, units, name = struct.unpack(">BBH12s", header)
        length = units * 16
        assert length >= 16 and offset + length <= end, (offset, length, end)
        # CHRP's end-around-carry header checksum, excluding its own byte.
        total = sum(header[:1] + header[2:])
        while total > 255:
            total = (total & 255) + (total >> 8)
        assert checksum == total, (offset, checksum, total)
        result.append((offset, signature, length, name.rstrip(b"\0")))
        offset += length
    assert offset == end
    return result


def check(qemu, firmware, seed_file=None):
    with tempfile.TemporaryDirectory(prefix="rb-osx-nvram-") as tmp:
        root = Path(tmp)
        images = {}
        for label, osx in (("classic", False), ("osx", True)):
            run = root / label
            run.mkdir()
            device = NVRAM(qemu, firmware, run, osx=osx)
            try:
                images[label] = device.read()
            finally:
                device.close()
        classic = partitions(images["classic"], 8192)
        assert classic[0][1:] == (0x70, 0xc10, b"common"), classic
        assert classic[1][1:] == (0xa0, 0x510, b"APL,MacOS75"), classic
        assert classic[2][1] == 0x7f, classic
        osx = partitions(images["osx"], 8192)
        assert osx[0][1:] == (0x70, 0xc10, b"system"), osx
        assert osx[1][1] == 0x7f and osx[1][0] + osx[1][2] == 4096, osx
        assert osx[2] == (4096, 0x5a, 4096, b"wwwwwwwwwww"), osx
        bank = images["osx"][4096:]
        assert struct.unpack_from(">I", bank, 20)[0] == 2
        assert struct.unpack_from(">I", bank, 16)[0] == zlib.adler32(bank[20:], 0)
        if seed_file is not None:
            assert seed_file.read_bytes() == images["osx"], "Package seed differs from mac99"

        backing = root / "nvram.raw"
        backing.write_bytes(images["osx"])
        for attempt in range(2):
            run = root / f"persistent-{attempt}"
            run.mkdir()
            device = NVRAM(qemu, firmware, run, osx=True, backing=backing)
            try:
                if attempt:
                    assert device.read()[0x1100] == 0xa5
                else:
                    device.write_byte(0x1100, 0xa5)
                    updated = device.read()[4096:]
                    checksum = struct.pack(">I", zlib.adler32(updated[20:], 0))
                    for index, value in enumerate(checksum):
                        device.write_byte(0x1010 + index, value)
                    assert device.read()[0x1100] == 0xa5
            finally:
                device.close()
        assert backing.read_bytes()[0x1100] == 0xa5
        invalid = root / "invalid.raw"
        invalid.write_bytes(bytes(8192))
        rejected = subprocess.run([
            str(qemu), "-M", "mac99,via=pmu,audiodev=snd0", "-S",
            "-audiodev", "none,id=snd0", "-display", "none",
            "-serial", "none", "-monitor", "none", "-nodefaults",
            "-L", str(firmware), "-global", "macio-nvram.osx-partitions=on",
            "-drive", f"if=none,id=nv,format=raw,file={invalid}",
            "-global", "macio-nvram.drive=nv"],
            capture_output=True, text=True, timeout=10)
        assert rejected.returncode != 0 and "no valid flash bank" in rejected.stderr
        assert invalid.read_bytes() == bytes(8192), "Invalid backing was overwritten"
        return {"classic_layout": "pass", "osx_dual_bank": "pass",
                "header_checksums": "pass", "osx_adler32": "pass",
                "persistent_write_relaunch": "pass",
                "invalid_backing_rejected_unchanged": "pass",
                "seed_sha256": hashlib.sha256(images["osx"]).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--seed-file", type=Path)
    args = parser.parse_args()
    print(json.dumps(check(args.qemu.resolve(), args.firmware.resolve(), args.seed_file),
                     indent=2))


if __name__ == "__main__":
    main()
