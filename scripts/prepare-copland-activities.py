#!/usr/bin/env python3
"""Build a NEW 512 MiB Copland activity disk from pinned public source assets.
Requires macOS hfsutils and unar. Never mounts the source disk itself.
Only use while no other hfsutils process/volume is active (global ~/.hcwd).
The output still requires real-guest qualification before publication.
"""
import argparse
import binascii
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'copland'))
from patch_clipboard import patch, PATCHED_SHA256
SOURCE_SHA = '383aa715435e02446b98ce478d1ce5729f77e8606b6657385bacba50970c1469'
MINES_SHA = '1ee39b5f081a4a7de12c9436af501b15cae5b845f639569f91e10d400c174d87'
ANARCHO_SHA = '863c4558aafc3ea3933cd6e32fc19747dd093aa8b955903bd3d8f65db0be6d82'
START = 3872 * 512
CAPACITY = 512 * 1024**2


def run(*args):
    return subprocess.check_output(args)


def checked(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError(f'Unexpected source hash: {path}')


def entries(path=b':'):
    result = []
    for line in run('hls', '-ilaN', path).split(b'\n'):
        if not line:
            continue
        match = re.match(rb'^\s*(\d+) ([dDfF])\S* .*? [A-Z][a-z]{2} +\d+ +(?:\d\d:\d\d|\d{4}) (.*)$', line)
        if not match:
            raise ValueError(f'Unrecognized hls record: {line!r}')
        cnid, kind, name = match.groups()
        item = path + name
        directory = kind.lower() == b'd'
        result.append((int(cnid), directory, item))
        if directory:
            result.extend(entries(item + b':'))
    return result


def export_tree(folder, selection=b':'):
    folder.mkdir()
    records = entries(selection)
    for cnid, directory, path in records:
        if not directory:
            run('hcopy', '-m', path, str(folder / f'{cnid}.bin'))
    return records


def fork_hash(path):
    b = path.read_bytes()
    data, resource = struct.unpack_from('>II', b, 83)
    rstart = 128 + ((data + 127) // 128) * 128
    return [hashlib.sha256(b[128:128+data]).hexdigest(),
            hashlib.sha256(b[rstart:rstart+resource]).hexdigest()]


def replace_data(binary, data):
    old = binary.read_bytes()
    header = bytearray(old[:128])
    length = struct.unpack_from('>I', header, 83)[0]
    resource = old[128 + ((length+127)//128)*128:]
    struct.pack_into('>I', header, 83, len(data))
    struct.pack_into('>H', header, 124, binascii.crc_hqx(header[:124], 0))
    binary.write_bytes(header + data + bytes(-len(data) % 128) + resource)


def text_binary(destination, content, temporary):
    name = destination.rsplit(':', 1)[-1].encode('mac_roman')
    data = content.replace('\r\n', '\n').replace('\r', '\n').replace('\n', '\r').encode('mac_roman', errors='replace')
    if len(data) > 30000 or not 1 <= len(name) <= 31:
        raise ValueError('Guest text exceeds classic TextEdit/name limits')
    header = bytearray(128)
    header[1] = len(name); header[2:2+len(name)] = name
    header[65:73] = b'TEXTntxt'  # Anarcho, not the incompatible SimpleText
    struct.pack_into('>I', header, 83, len(data))
    struct.pack_into('>II', header, 91, 3881606400, 3881606400)
    header[122:124] = bytes([129, 129])
    struct.pack_into('>H', header, 124, binascii.crc_hqx(header[:124], 0))
    temporary.write_bytes(header + data + bytes(-len(data) % 128))
    run('hcopy', '-m', str(temporary), destination)


def normalize(image):
    """Copland explicitly requires obsolete filStBlk/filRStBlk == 0.
    hfsutils sets these pre-HFS legacy fields. Actual fork extents stay intact.
    """
    b = bytearray(image.read_bytes())
    mdb = START + 1024
    block_size = struct.unpack_from('>I', b, mdb + 20)[0]
    allocation = START + struct.unpack_from('>H', b, mdb + 28)[0] * 512
    extents = struct.unpack_from('>6H', b, mdb + 150)
    pieces = [(allocation + extents[i] * block_size, extents[i+1] * block_size) for i in (0, 2, 4)]
    cat = b''.join(b[start:start+size] for start, size in pieces)
    catalog_size = struct.unpack_from('>I', b, mdb + 146)[0]
    if len(cat) < catalog_size:
        raise ValueError('Catalog uses unsupported overflow extents')
    cat = cat[:catalog_size]
    node_size = struct.unpack_from('>H', cat, 32)[0]
    if node_size != 512:
        raise ValueError('Unexpected classic HFS B-tree node size')
    def physical(offset):
        for start, size in pieces:
            if offset < size:
                return start + offset
            offset -= size
        raise ValueError('Invalid catalog offset')
    for base in range(0, len(cat), node_size):
        node = cat[base:base+node_size]
        if node[8] != 255:
            continue
        count = struct.unpack_from('>H', node, 10)[0]
        for record in range(count):
            start = struct.unpack_from('>H', node, node_size - 2 * (record+1))[0]
            end = struct.unpack_from('>H', node, node_size - 2 * (record+2))[0]
            if not 14 <= start < end <= node_size - 2 * (count+1):
                raise ValueError('Invalid catalog record')
            data = start + ((node[start] + 2) & ~1)
            if node[data] == 2:
                for offset in (24, 34):
                    if data + offset + 2 > end:
                        raise ValueError('Truncated catalog file record')
                    for byte in (0, 1):
                        b[physical(base + data + offset + byte)] = 0
    image.write_bytes(b)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--mines', type=Path, required=True)
    p.add_argument('--anarcho', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    for path, digest in [(a.source, SOURCE_SHA), (a.mines, MINES_SHA), (a.anarcho, ANARCHO_SHA)]:
        checked(path, digest)
    if a.output.exists():
        p.error('Output must be a new path')
    if subprocess.run(['hpwd'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        p.error('Unmount the current hfsutils volume first')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='copland-build-', dir=a.output.parent) as temporary:
        work = Path(temporary)
        source = work / 'source.img'
        shutil.copyfile(a.source, source)
        try:
            run('hmount', str(source))
            original = export_tree(work / 'original')
            run('humount')
            clipboard = work / 'original' / '222.bin'
            raw = clipboard.read_bytes()
            replace_data(clipboard, patch(raw[128:128+struct.unpack_from('>I', raw, 83)[0]]))
            source_bytes = a.source.read_bytes()
            prefix = bytearray(source_bytes[:START])
            struct.pack_into('>I', prefix, 4, CAPACITY // 512)
            for i in range(1, 12):
                o = i * 512
                if prefix[o+48:o+80].split(b'\0')[0] == b'Apple_HFS':
                    for offset in (12, 84):
                        struct.pack_into('>I', prefix, o+offset, (CAPACITY-START)//512)
            with a.output.open('xb') as f:
                f.write(prefix)
                f.truncate(CAPACITY)
            run('hformat', '-l', 'Copland HD', str(a.output))
            # Keeping original catalog IDs preserves system-folder references.
            expected = {}
            for cnid, directory, path in sorted(original):
                with a.output.open('r+b') as f:
                    for mdb in (START+1024, CAPACITY-1024):
                        f.seek(mdb+30)
                        f.write(struct.pack('>I', cnid))
                if directory:
                    run('hmkdir', path)
                else:
                    binary = work / 'original' / f'{cnid}.bin'
                    run('hcopy', '-m', str(binary), path)
                    expected[path] = fork_hash(binary)
            run('hattrib', '-b', ':System Folder')
            run('humount')
            with a.output.open('r+b') as f:
                f.seek(START)
                f.write(source_bytes[START:START+1024])
                for mdb in (START+1024, CAPACITY-1024):
                    f.seek(mdb+2)
                    f.write(source_bytes[START+1026:START+1030])
                    f.seek(mdb+92)
                    f.write(source_bytes[START+1116:START+1148])
            run('unar', '-quiet', '-o', str(work/'mines'), str(a.mines))
            mines = next((work/'mines').rglob('MineSweeper'))
            run('unar', '-quiet', '-o', str(work/'anarcho'), str(a.anarcho))
            anarcho = next((work/'anarcho').rglob('Anarcho'))
            run('hmount', str(a.output))
            run('hmkdir', ':Desktop Folder')
            run('hmkdir', ':Desktop Folder:Activities')
            moves = [(b":Applications:Eric's Solitaire", b":Desktop Folder:Activities:Solitaire"),
                     (b':Applications:GXSlidemaster', b':Desktop Folder:Activities:GXSlidemaster')]
            for old, new in moves:
                run('hrename', old, new)
                for key in list(expected):
                    if key == old or key.startswith(old+b':'):
                        expected[new+key[len(old):]] = expected.pop(key)
            run('python3', str(ROOT/'guestcd/hfs-copy.py'), str(mines.parent), ':Desktop Folder:Activities:MineSweeper')
            run('python3', str(ROOT/'guestcd/hfs-copy.py'), str(anarcho.parent), ':Desktop Folder:Activities:Anarcho')
            run('hmkdir', ':Desktop Folder:Activities:Reading')
            run('hmkdir', ':Desktop Folder:Activities:My Documents')
            generated = work / 'generated.bin'
            for name in ('Read Me First', 'Experiments', 'Notebook'):
                target = ':Desktop Folder:Activities:' + ('My Documents:' if name == 'Notebook' else '') + name
                text_binary(target, (ROOT/'copland/activities'/f'{name}.txt').read_text(), generated)
                expected[target.encode('mac_roman')] = fork_hash(generated)
            for guide in sorted((ROOT/'copland/activities/reading').glob('*.txt')):
                target = ':Desktop Folder:Activities:Reading:' + guide.stem
                text_binary(target, guide.read_text(), generated)
                expected[target.encode('mac_roman')] = fork_hash(generated)
            # Verify each added application's forks against the extracted archive.
            for local, target in [(mines, b':Desktop Folder:Activities:MineSweeper:MineSweeper'),
                                  (anarcho, b':Desktop Folder:Activities:Anarcho:Anarcho')]:
                check = work/'app-check.bin'
                run('hcopy', '-m', target, str(check))
                source_hashes = [hashlib.sha256(local.read_bytes()).hexdigest(),
                                 hashlib.sha256(Path(str(local)+'/..namedfork/rsrc').read_bytes()).hexdigest()]
                if fork_hash(check) != source_hashes:
                    raise ValueError(f'Application fork verification failed: {target!r}')
            # Verify both forks of every original/generated file after copying.
            check = work/'verify.bin'
            for path, digest in expected.items():
                run('hcopy', '-m', path, str(check))
                if fork_hash(check) != digest:
                    raise ValueError(f'Fork verification failed: {path!r}')
            inventory = run('hls', '-RilaN', ':').decode('mac_roman')
            run('humount')
            normalize(a.output)
            manifest = dict(sourceSHA256=SOURCE_SHA, minesSHA256=MINES_SHA,
                            anarchoSHA256=ANARCHO_SHA, clipboardSHA256=PATCHED_SHA256,
                            diskSHA256=hashlib.sha256(a.output.read_bytes()).hexdigest(),
                            diskBytes=a.output.stat().st_size, verifiedFiles=len(expected))
            a.output.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n')
            a.output.with_suffix('.txt').write_text(inventory)
            print(json.dumps(manifest, indent=2))
        finally:
            subprocess.run(['humount'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == '__main__':
    main()
