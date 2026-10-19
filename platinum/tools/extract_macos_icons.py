#!/usr/bin/env python3
"""Read classic resource forks and preserve selected Apple icon members.

Extraction: --system-folder '/Volumes/.../System Folder' --icon-services-dir DIR
Regeneration from the committed icon families: no arguments (stdlib only).
--check verifies provenance, PNG exports and the embedded Swift assets.
No System/Finder executable, font or complete resource fork is redistributed.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'platinum/macos-icons'
SWIFT = ROOT / 'app/Sources/PlatinumKit/Generated/MacOSIconAssets.swift'
DESKTOP = ROOT / 'desktop/ui/mac-icons'

# Each size is an original strike, never a resampling of the other size.
SELECTION = {
    'cd': {32: ('System', -3987), 16: ('Finder', 4600)},
    'disk': {32: ('System', -3998), 16: ('System', -3998)},
    'folder': {32: ('System', -3999), 16: ('System', -3999)},
    'folder_open': {32: ('System', -3997), 16: ('System', -3997)},
    'library': {32: ('System', -3970), 16: ('System', -3970)},
    'web': {32: ('System', -20785), 16: ('System', -20785)},
    'display': {32: ('Control Panels/Monitors', 128), 16: ('Control Panels/Monitors', 128)},
    'clipboard': {32: ('System', -16509), 16: ('System', -16509)},
    'caution': {32: ('Finder', 7201), 16: ('Finder', 7222)},
    'stop': {32: ('Finder', 7203), 16: ('Finder', 25015)},
    'note': {32: ('System', -32511), 16: ('System', -32511)},
    'lock': {32: ('System', -3773), 16: ('System', -3773)},
    'trash': {32: ('System', -3993), 16: ('System', -3993)},
    'harddisk': {32: ('System', -20725), 16: ('System', -20725)},
    'preferences': {32: ('System', -3971), 16: ('System', -3971)},
    'mac': {32: ('System', 3), 16: ('System', 3)},
}
ICON_SERVICES = {'cd':'cddr','disk':'flpy','folder':'fldr','folder_open':'ofld',
                 'web':'gnet','clipboard':'CLIP','caution':'caut','stop':'stop',
                 'note':'note','trash':'trsh','harddisk':'hdsk','preferences':'pref'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def resources(data):
    """Resource Manager map; reject compressed members rather than misdecode."""
    def part(at, size):
        if at < 0 or size < 0 or at + size > len(data):
            raise ValueError('Resource fork bounds')
        return data[at:at+size]
    da, ma, dl, ml = struct.unpack('>4I', part(0, 16))
    part(da, dl); part(ma, ml)
    types = ma + struct.unpack('>H', part(ma+24, 2))[0]
    count = struct.unpack('>H', part(types, 2))[0] + 1
    result = {}
    for i in range(count):
        kind, n, offset = struct.unpack('>4sHH', part(types+2+i*8, 8))
        for j in range(n+1):
            ref = part(types+offset+j*12, 12)
            rid = struct.unpack('>h', ref[:2])[0]
            if ref[4] & 1:
                continue
            start = da + int.from_bytes(ref[5:8], 'big')
            size = struct.unpack('>I', part(start, 4))[0]
            if start < da or start + 4 + size > da + dl:
                raise ValueError('Resource data bounds')
            result[kind.decode('mac_roman'), rid] = part(start+4, size)
    return result


def members(data):
    if data[:4] != b'icns' or len(data) != struct.unpack_from('>I', data, 4)[0]:
        raise ValueError('Invalid icon family')
    result = {}; at = 8
    while at < len(data):
        kind, size = struct.unpack_from('>4sI', data, at)
        if size < 8 or at + size > len(data):
            raise ValueError('Icon member bounds')
        result[kind.decode('mac_roman')] = data[at+8:at+size]
        at += size
    return result


RAMP = [238, 221, 187, 170, 136, 119, 85, 68, 34, 17]
PALETTE8 = [((5-i//36)*51, (5-(i//6)%6)*51, (5-i%6)*51) for i in range(215)]
PALETTE8 += [(n,0,0) for n in RAMP] + [(0,n,0) for n in RAMP] + [(0,0,n) for n in RAMP]
PALETTE8 += [(n,n,n) for n in RAMP] + [(0,0,0)]
PALETTE4 = [tuple(bytes.fromhex(c)) for c in
            'ffffff fcf305 ff6402 dd0806 f20884 4600a5 0000d4 02abea '
            '1fb714 006411 562c05 90713a c0c0c0 808080 404040 000000'.split()]


def rgba(family, size):
    eight, four, bits = ('icl8', 'icl4', 'ICN#') if size == 32 else ('ics8', 'ics4', 'ics#')
    n = size*size
    rgb, alpha = ('il32','l8mk') if size == 32 else ('is32','s8mk')
    if rgb in family and alpha in family:
        source = family[rgb]; at = 0; planes = []
        if len(source) == n*3:
            planes = [source[i*n:(i+1)*n] for i in range(3)]
        else:
            for _ in range(3):
                plane = bytearray()
                while len(plane) < n:
                    control = source[at]; at += 1
                    if control >= 128:
                        plane.extend(bytes([source[at]])*(control-125)); at += 1
                    else:
                        count = control+1; plane.extend(source[at:at+count]); at += count
                assert len(plane) == n
                planes.append(plane)
            assert at == len(source)
        mask = family[alpha]; assert len(mask) == n
        return b''.join(bytes((planes[0][i],planes[1][i],planes[2][i],mask[i]))
                        if mask[i] else b'\0\0\0\0' for i in range(n))
    mono = family[bits]
    if len(mono) != n//4:
        raise ValueError('Missing original icon mask')
    mask = mono[n//8:]
    if eight in family:
        color = family[eight]; assert len(color) == n
        pixels = [PALETTE8[i] for i in color]
    elif four in family:
        color = family[four]; assert len(color) == n//2
        pixels = [PALETTE4[(color[i//2] >> (4 if i%2 == 0 else 0)) & 15] for i in range(n)]
    else:
        pixels = [(0,0,0) if mono[i//8] & (128 >> (i%8)) else (255,255,255) for i in range(n)]
    mask8 = family.get(alpha)
    if mask8 is not None: assert len(mask8) == n
    alphas = mask8 if mask8 is not None else [255 if mask[i//8] & (128 >> (i%8)) else 0 for i in range(n)]
    return b''.join(bytes((*p,alphas[i])) if alphas[i] else b'\0\0\0\0' for i,p in enumerate(pixels))


def png(pixels, size):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data))
    rows = b''.join(b'\0'+pixels[y*size*4:(y+1)*size*4] for y in range(size))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB',size,size,8,6,0,0,0))
            + chunk(b'IDAT',zlib.compress(rows,9)) + chunk(b'IEND',b''))


def extract(folder, icon_services):
    OUT.mkdir(parents=True, exist_ok=True)
    sources = {}; source_info = {}; icons = {}
    for path in sorted({s[0] for choices in SELECTION.values() for s in choices.values()}):
        raw = (folder/path/'..namedfork/rsrc').read_bytes()
        sources[path] = resources(raw)
        version = sources[path].get(('vers',1),b'')
        source_info[path] = {'resource_fork_sha256':sha(raw),
                            'version':version[7:7+version[6]].decode('mac_roman') if len(version)>7 else None}
    for name, choices in SELECTION.items():
        if name in ICON_SERVICES:
            # Icon Services supplies the installed Mac OS 9 colour/alpha
            # families, including resources not present in the flat System map.
            code = ICON_SERVICES[name]
            family = (icon_services/f'Native-{code}.icns').read_bytes(); decoded = members(family)
            for size in [16,32]: rgba(decoded,size)
            (OUT/(name+'.icns')).write_bytes(family)
            icons[name] = {'sizes':[16,32], 'icns_sha256':sha(family),
                'icon_services_type':code,
                'members':[{'type':kind,'source':'Mac OS 9.2.1 Icon Services',
                            'container':'IconRefToIconFamily(kSelectorAllAvailableData)','sha256':sha(raw)}
                           for kind,raw in decoded.items()]}
            continue
        parts = []; info = []
        for size,(source,rid) in sorted(choices.items()):
            kinds = ('ics#','ics4','ics8','is32','s8mk') if size == 16 else ('ICN#','icl4','icl8','il32','l8mk')
            packed = sources[source].get(('icns',rid))
            source_members = members(packed) if packed else None
            for kind in kinds:
                raw = source_members.get(kind) if source_members is not None else sources[source].get((kind,rid))
                if raw is None: continue
                parts.append(kind.encode('mac_roman')+struct.pack('>I',len(raw)+8)+raw)
                info.append({'type':kind,'id':rid,'source':source,'container':'icns' if packed else kind,'sha256':sha(raw)})
        body = b''.join(parts); family = b'icns'+struct.pack('>I',len(body)+8)+body
        (OUT/(name+'.icns')).write_bytes(family)
        icons[name] = {'sizes':sorted(choices),'icns_sha256':sha(family),'members':info}
    manifest = {'origin':'Native Mac OS 9.2.1 Icon Services; additional Mac OS 9.2.2 System / Finder / Monitors resources',
                'source_fixture':'public OS9 fixture / SuperDeluxe v2; resource images mounted read-only',
                'copyright':'Apple Computer, Inc.; these assets are not MIT licensed',
                'sources':source_info,'icons':icons}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')


def generate(check=False):
    manifest = json.loads((OUT/'manifest.json').read_text()); encoded = {}
    DESKTOP.mkdir(parents=True,exist_ok=True)
    def write(path, data):
        if check:
            assert path.read_bytes() == data, str(path.relative_to(ROOT))+' is stale'
        else:
            path.write_bytes(data)
    for name,info in sorted(manifest['icons'].items()):
        data = (OUT/(name+'.icns')).read_bytes(); assert sha(data)==info['icns_sha256']
        family = members(data)
        for member in info['members']:
            assert sha(family[member['type']])==member['sha256']
        for size in info['sizes']:
            image = png(rgba(family,size),size)
            write(DESKTOP/f'{name}-{size}.png',image)
            encoded[f'{name}@{size}'] = base64.b64encode(image).decode()
    code = '// Generated by platinum/tools/extract_macos_icons.py.\n'
    code += '// Original Apple artwork; provenance and copyright: platinum/macos-icons/.\n'
    code += 'enum MacOSIconAssets {\n    static let png: [String: String] = [\n'
    code += ''.join(f'        "{name}": "{data}",\n' for name,data in encoded.items())
    code += '    ]\n}\n'
    write(SWIFT,code.encode())
    print(f'{len(encoded)} native icon strikes '+('verified' if check else 'exported'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--system-folder', type=Path)
    parser.add_argument('--icon-services-dir', type=Path, help='Directory of Native-*.icns from the native reference utility')
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    if args.system_folder:
        assert not args.check
        if not args.icon_services_dir: parser.error('--system-folder requires --icon-services-dir for native colour/alpha families')
        extract(args.system_folder, args.icon_services_dir)
    generate(args.check)
