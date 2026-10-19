#!/usr/bin/env python3
"""Synthetic check of the screamer output DMA pacing (no guest software).

Mac OS 9's sound driver double-buffers through DBDMA: LOAD_WORD of the
screamer frame counter, OUTPUT one half of a ring, STORE_WORD a "half done"
flag with an interrupt, the same for the other half, STOP.  It only works if
the halves complete one after the other at the codec's pace (see
screamer/patches/retroboot-screamer-paced-dma.patch and
screamer/README.md).  This test runs QEMU with only
OpenBIOS (which maps MacIO), stops the VM, writes exactly such a program
into guest RAM, starts DBDMA channel 0x10 and checks:

  1. with the virtual clock stopped, no half completes (QEMU used to finish
     the whole program at once, before any guest time had passed);
  2. once the VM runs, half A completes, then half B one half-duration later
     (11.6 ms for 2048 bytes at 44.1 kHz; 4-40 ms accepted);
  3. the two LOAD_WORD frame-counter stamps differ by the frames of half A;
  4. pace: a looping program (frame-counter stamp, two 1 ms output
     commands, branch back) takes 44,100 frames per second of running
     time within 0.5 %.  Package r12/r13 completed each command one play
     time after it started, so every timer delay was lost for good: about
     2 % slow on an idle host, 10-40 % under load, and the host stream,
     played in real time, ran dry (screamer/README.md);
  5. hold: a looping two-half program with a half-done interrupt each; once
     the status has been read after an interrupt (as Mac OS does), output
     waits while the last interrupt is unserviced (screamer.irq-hold-ms,
     at most 100 ms each): without status reads the program runs at most
     at a quarter of real time, with a status read after every interrupt
     at full speed.

    qtest_screamer.py --qemu BIN --share DIR --run NEWDIR [--expect instant]

--expect instant inverts checks 1-2 and skips 4 (screamer.paced-dma=off,
for showing the old behaviour).  Writes RUN/result.json; exit status 1 on
failure.
Python standard library only.
"""
import argparse
import json
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

PROG = 0x02000000          # DBDMA program
FLAG = 0x02000ff0          # STORE_WORD target ("which half is done")
LOOP = 0x02001000          # pace: looping program
HOLDP = 0x02002000         # hold: looping two-half program with interrupts
PACE_S = 3.0
BUF = 0x02010000           # two 2048-byte halves
HALF = 2048
CHUNK = 512
RATE = 44100


def connect(path, timeout=20):
    deadline = time.monotonic() + timeout
    while True:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            s.connect(str(path))
            return s
        except OSError:
            s.close()
            if time.monotonic() > deadline:
                raise
            time.sleep(0.1)


class QMP:
    def __init__(self, path):
        self.f = connect(path).makefile('rw')
        self.read()
        self.call('qmp_capabilities')

    def read(self):
        while True:
            obj = json.loads(self.f.readline())
            if 'event' not in obj:
                return obj

    def call(self, cmd, **args):
        self.f.write(json.dumps({'execute': cmd, 'arguments': args}) + '\n')
        self.f.flush()
        r = self.read()
        if 'error' in r:
            raise RuntimeError(r['error'])
        return r.get('return')


class QTest:
    def __init__(self, path):
        self.f = connect(path).makefile('rw')

    def raw(self, line):
        self.f.write(line + '\n')
        self.f.flush()
        reply = self.f.readline().strip()
        if not reply.startswith('OK'):
            raise RuntimeError(f'{line}: {reply}')
        return reply

    def write(self, addr, data: bytes):
        self.raw(f'write 0x{addr:x} {len(data)} 0x{data.hex()}')

    def read(self, addr, n) -> bytes:
        return bytes.fromhex(self.raw(f'read 0x{addr:x} {n}').split()[1][2:])

    def writel_le(self, addr, value):
        # qtest writel uses the CPU's (big-endian) order; DBDMA registers are LE
        self.raw(f'writel 0x{addr:x} 0x{struct.unpack("<I", struct.pack(">I", value))[0]:x}')


def cmd(op, key, req, addr, dep=0, i=0, b=0):
    command = (op << 12) | (key << 8) | (i << 4) | (b << 2)
    return struct.pack('<HHIIHH', req, command, addr, dep, 0, 0)


def program(screamer_base):
    LOAD, STORE, OUT_LAST, STOP, KEY_SYSTEM, ALWAYS = 5, 4, 1, 7, 6, 3
    p = b''
    for half in range(2):
        p += cmd(LOAD, KEY_SYSTEM, 4, screamer_base + 0x50)
        for c in range(HALF // CHUNK):
            p += cmd(OUT_LAST, 0, CHUNK, BUF + half * HALF + c * CHUNK)
        p += cmd(STORE, KEY_SYSTEM, 4, FLAG, dep=half, i=ALWAYS)
    p += cmd(STOP, 0, 0, 0)
    return p


def loop_program(screamer_base):
    LOAD, OUT_MORE, NOP, KEY_SYSTEM, ALWAYS = 5, 0, 6, 6, 3
    return (cmd(LOAD, KEY_SYSTEM, 4, screamer_base + 0x50, 0) +
            cmd(OUT_MORE, 0, 176, BUF) + cmd(OUT_MORE, 0, 176, BUF + 176) +
            cmd(NOP, 0, 0, 0, dep=LOOP, b=ALWAYS))


def hold_program(screamer_base):
    LOAD, STORE, OUT_MORE, NOP, KEY_SYSTEM, ALWAYS = 5, 4, 0, 6, 6, 3
    p = b''
    for half in range(2):
        p += cmd(LOAD, KEY_SYSTEM, 4, screamer_base + 0x50)
        for c in range(HALF // CHUNK):
            p += cmd(OUT_MORE, 0, CHUNK, BUF + half * HALF + c * CHUNK)
        p += cmd(STORE, KEY_SYSTEM, 4, FLAG, dep=half, i=ALWAYS)
    return p + cmd(NOP, 0, 0, 0, dep=HOLDP, b=ALWAYS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--qemu', required=True)
    ap.add_argument('--share', required=True, help="the package's share/qemu (OpenBIOS)")
    ap.add_argument('--run', required=True, type=Path)
    ap.add_argument('--expect', choices=['paced', 'instant'], default='paced')
    a = ap.parse_args()
    a.run.mkdir(parents=True, exist_ok=False)
    argv = [a.qemu, '-M', 'mac99,via=cuda,audiodev=snd0', '-cpu', '7400', '-m', '128',
            '-display', 'none', '-nic', 'none', '-audiodev', 'none,id=snd0', '-L', a.share,
            '-qmp', f'unix:{a.run}/qmp.sock,server=on,wait=off',
            '-qtest', f'unix:{a.run}/qtest.sock,server=on,wait=off']
    if a.expect == 'instant':
        argv += ['-global', 'screamer.paced-dma=off']
    checks, result = [], {'origin': 'synthetic-qtest', 'apple_software_run': False, 'expect': a.expect}

    def check(ok, what):
        checks.append({'check': what, 'ok': bool(ok)})

    proc = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=open(a.run / 'stderr.log', 'w'))
    try:
        q = QMP(a.run / 'qmp.sock')
        base = {}
        for _ in range(90):                 # OpenBIOS enumerates PCI and maps MacIO
            tree = q.call('human-monitor-command', **{'command-line': 'info mtree -f'})
            for name in ('screamer', 'dbdma'):
                m = re.search(r'([0-9a-f]{8,16})-[0-9a-f]{8,16} \(prio [-0-9]+, i/o\): ' + name + r'\b', tree)
                if m:
                    base[name] = int(m[1], 16)
            if len(base) == 2:
                break
            time.sleep(0.5)
        if len(base) != 2:
            (a.run / 'mtree.txt').write_text(tree)
            raise RuntimeError('screamer/dbdma not mapped; see mtree.txt')
        result['screamer'] = hex(base['screamer'])
        result['dbdma'] = hex(base['dbdma'])
        q.call('stop')
        t = QTest(a.run / 'qtest.sock')
        t.write(BUF, bytes(range(256)) * (2 * HALF // 256))
        t.write(FLAG, b'\xff\xff\xff\xff')
        t.write(PROG, program(base['screamer']))
        ch = base["dbdma"] + (0x10 << 7)              # 128-byte channel blocks
        t.writel_le(ch + 0x0c, PROG)                 # CMDPTR_LO
        t.writel_le(ch + 0x00, 0x80008000)           # CONTROL: RUN
        time.sleep(0.3)                              # virtual clock stopped
        flag = struct.unpack('<I', t.read(FLAG, 4))[0]
        result['flag_while_stopped'] = hex(flag)
        if a.expect == 'paced':
            check(flag == 0xffffffff, 'no half completes while the virtual clock is stopped')
        else:
            check(flag == 1, 'old behaviour: the whole program completes at once')
        q.call('cont')
        t0 = time.monotonic()
        seen = {}
        while time.monotonic() - t0 < 1.0 and 1 not in seen:
            flag = struct.unpack('<I', t.read(FLAG, 4))[0]
            if flag in (0, 1) and flag not in seen:
                seen[flag] = time.monotonic() - t0
            time.sleep(0.0005)
        q.call('stop')
        result['half_done_ms'] = {str(k): round(v * 1000, 2) for k, v in seen.items()}
        stamp_a = struct.unpack('<I', t.read(PROG + 8, 4))[0]
        stamp_b = struct.unpack('<I', t.read(PROG + 16 * (HALF // CHUNK + 2) + 8, 4))[0]
        result['frame_stamps'] = [stamp_a, stamp_b]
        check(stamp_b - stamp_a == HALF // 4, f'frame-counter stamps one half ({HALF // 4} frames) apart')
        if a.expect == 'paced':
            half_ms = HALF / 4 / RATE * 1000
            check(0 in seen and 1 in seen, 'both halves complete once the VM runs')
            gap = (seen.get(1, 0) - seen.get(0, 0)) * 1000
            result['half_gap_ms'] = round(gap, 2)
            check(4 <= gap <= 40, f'half B completes about one half ({half_ms:.1f} ms) after half A')

            # pace: frames per second of running time, over PACE_S seconds
            t.writel_le(ch + 0x00, 0x80000000)       # CONTROL: clear RUN
            t.write(LOOP, loop_program(base['screamer']))
            t.writel_le(ch + 0x0c, LOOP)
            t.writel_le(ch + 0x00, 0x80008000)       # RUN
            q.call('cont')
            time.sleep(0.5)                          # past the start-up catch-up

            def stamp():
                t0 = time.monotonic()
                v = struct.unpack('<I', t.read(LOOP + 8, 4))[0]
                return (t0 + time.monotonic()) / 2, v
            ta, fa = stamp()
            time.sleep(PACE_S)
            tb, fb = stamp()
            q.call('stop')
            rate = (fb - fa) / (tb - ta)
            result['pace_frames_per_s'] = round(rate)
            check(abs(rate / RATE - 1) < 0.005,
                  f'the looping program takes {RATE} frames per second within 0.5 % (no slip)')

            # hold: interrupts the guest does or does not service
            t.writel_le(ch + 0x00, 0x80000000)       # CONTROL: clear RUN
            t.write(FLAG, b'\xff\xff\xff\xff')
            t.write(HOLDP, hold_program(base['screamer']))
            t.writel_le(ch + 0x0c, HOLDP)
            t.writel_le(ch + 0x00, 0x80008000)       # RUN
            q.call('cont')
            deadline = time.monotonic() + 1.0
            while struct.unpack('<I', t.read(FLAG, 4))[0] == 0xffffffff and time.monotonic() < deadline:
                time.sleep(0.001)
            t.raw(f'readl 0x{ch + 0x04:x}')          # service it: the status read

            def frames_over(seconds, service):
                fa = struct.unpack('<I', t.read(HOLDP + 8, 4))[0]
                end, last = time.monotonic() + seconds, None
                while time.monotonic() < end:
                    flag = struct.unpack('<I', t.read(FLAG, 4))[0]
                    if service and flag != last:
                        t.raw(f'readl 0x{ch + 0x04:x}')
                        last = flag
                    time.sleep(0.001)
                fb = struct.unpack('<I', t.read(HOLDP + 8, 4))[0]
                return (fb - fa) / seconds
            unserviced = frames_over(1.0, False)
            serviced = frames_over(1.0, True)
            q.call('stop')
            result['hold_frames_per_s'] = {'unserviced': round(unserviced), 'serviced': round(serviced)}
            check(unserviced < RATE / 4, 'output waits while the last interrupt is unserviced')
            check(serviced > RATE * 0.9, 'serviced interrupts: output at full speed')
    except Exception as exc:  # noqa: BLE001
        checks.append({'check': f'error: {exc}', 'ok': False})
    finally:
        proc.kill()
        proc.wait(10)
    result['checks'] = checks
    result['pass'] = bool(checks) and all(c['ok'] for c in checks)
    (a.run / 'result.json').write_text(json.dumps(result, indent=1))
    print(json.dumps(result, indent=1))
    sys.exit(0 if result['pass'] else 1)


if __name__ == '__main__':
    main()
