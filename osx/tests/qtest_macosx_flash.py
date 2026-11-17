#!/usr/bin/env python3
"""Exercise the native Core99 flash command protocol and persistent banks.

No Apple guest code is used. The actual device is driven like a flash
master; these checks do not replace native OS X preferences/reboot proof.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib
from qtest_macosx_nvram import NVRAM, partitions


def valid(bank):
    assert len(bank) == 8192
    parts = partitions(bank, 8192)
    assert parts[0] == (0, 0x5a, 32, b"wwwwwwwwwwww")
    assert parts[1] == (32, 0x70, 0xc10, b"common")
    assert struct.unpack_from(">I", bank, 16)[0] == zlib.adler32(bank[20:], 1)


def check(qemu, firmware, seed_file):
    with tempfile.TemporaryDirectory(prefix="rb-core99-flash-") as temp:
        root = Path(temp)
        run = root / "fresh"
        run.mkdir()
        q = NVRAM(qemu, firmware, run, flash=True)
        try:
            original = q.read()
        finally:
            q.close()
        assert len(original) == 16384
        for index in range(2):
            chunk = original[index * 8192:(index + 1) * 8192]
            valid(chunk)
            assert struct.unpack_from(">I", chunk, 20)[0] == index + 1
        assert seed_file.read_bytes() == original, "Seed differs from device"
        backing = root / "nvram.img"
        backing.write_bytes(original)
        run = root / "commands"
        run.mkdir()
        q = NVRAM(qemu, firmware, run, flash=True, backing=backing)
        try:
            assert q.read() == original
            # Ordinary writes are commands, never a RAM shortcut.
            q.write_byte(0x1600, 0x12)
            assert q.read()[0] == 0xb0
            q.write_byte(0, 0xff)
            assert q.read() == original
            q.write_byte(0, 0x50)
            assert q.read()[0] == 0x80
            q.write_byte(0, 0xff)
            q.write_byte(0, 0x20)
            q.write_byte(0, 0xd0)
            assert q.read()[0] == 0x80
            q.write_byte(0, 0xff)
            assert q.read()[:8192] == bytes([255]) * 8192
            assert q.read()[8192:] == original[8192:]

            q.write_byte(0x1600, 0x40)
            q.write_byte(0x1600, 0x55)
            assert q.read()[0] == 0x80
            q.write_byte(0, 0xff)
            assert q.read()[0x1600] == 0x55
            q.write_byte(0x1600, 0x40)
            q.write_byte(0x1600, 0xff)  # Cannot change zero bits back to one.
            assert q.read()[0] == 0x90
            q.write_byte(0, 0xff)
            assert q.read()[0x1600] == 0x55
            q.write_byte(0, 0x50)
            q.write_byte(0, 0x20)
            q.write_byte(0, 0xd0)
            q.write_byte(0, 0xff)
            # A complete new bank, with a new generation and observable byte.
            updated = bytearray(original[8192:])
            struct.pack_into(">I", updated, 20, 3)
            updated[0x1600] = 0xa5
            struct.pack_into(">I", updated, 16, zlib.adler32(updated[20:], 1))
            for offset, value in enumerate(updated):
                q.write_byte(offset, 0x40)
                q.write_byte(offset, value)
            assert q.read()[0] == 0x80
            q.write_byte(0, 0xff)
            assert q.read()[:8192] == updated
            assert q.read()[8192:] == original[8192:]
        finally:
            q.close()
        valid(backing.read_bytes()[:8192])
        run = root / "relaunch"
        run.mkdir()
        q = NVRAM(qemu, firmware, run, flash=True, backing=backing)
        try:
            assert q.read()[:8192] == updated
        finally:
            q.close()
        interrupted = root / "interrupted.img"
        interrupted.write_bytes(bytes([255]) * 8192 + original[8192:])
        run = root / "one-valid-bank"
        run.mkdir()
        q = NVRAM(qemu, firmware, run, flash=True, backing=interrupted)
        try:
            assert q.read() == interrupted.read_bytes()
        finally:
            q.close()
        invalid = root / "invalid.img"
        invalid.write_bytes(bytes(16384))
        result = subprocess.run([
            str(qemu), "-M", "mac99,via=pmu,audiodev=snd0", "-S",
            "-audiodev", "none,id=snd0", "-display", "none", "-serial", "none",
            "-monitor", "none", "-nodefaults", "-L", str(firmware),
            "-bios", str(firmware / "openbios-ppc-osx"),
            "-global", "macio-nvram.osx-flash=on", "-drive",
            f"if=none,id=nv,format=raw,file={invalid}",
            "-global", "macio-nvram.drive=nv"],
            capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and "no valid flash bank" in result.stderr
        assert invalid.read_bytes() == bytes(16384)
        return {"bank_layout_checksums": "pass", "erase_program_status": "pass",
                "program_cannot_set_bits": "pass", "other_bank_preserved": "pass",
                "persistent_write_relaunch": "pass", "interrupted_bank": "pass",
                "invalid_rejected_unchanged": "pass",
                "seed_sha256": hashlib.sha256(original).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--seed-file", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.qemu.resolve(), args.firmware.resolve(),
                           args.seed_file.resolve()), indent=2))


if __name__ == "__main__":
    main()
