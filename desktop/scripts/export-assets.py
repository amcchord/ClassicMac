#!/usr/bin/env python3
"""Export the shared MIT fonts and exact 16/32px icns artwork for the PC UI."""
from pathlib import Path
import importlib.util
import struct
import zlib

root = Path(__file__).resolve().parents[2]
out = root / 'desktop/ui'
spec = importlib.util.spec_from_file_location('platinum', root / 'platinum/tools/build_platinum_swift.py')
fonts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fonts)
for name in ('System', 'Regular'):
    strike = fonts.parse_bdf(root / f'platinum/fonts/RetroBoot-{name}-12.bdf')
    (out / f'RetroBoot-{name}.ttf').write_bytes(fonts.build_ttf(strike, f'RetroBoot {name}', 'Regular', f'RetroBoot-{name}', 400))

palette = []
for i in range(215):
    palette.append(((5-i//36)*51, (5-(i//6)%6)*51, (5-i%6)*51))
ramp = [238, 221, 187, 170, 136, 119, 85, 68, 34, 17]
palette += [(n, 0, 0) for n in ramp] + [(0, n, 0) for n in ramp] + [(0, 0, n) for n in ramp] + [(n, n, n) for n in ramp] + [(0, 0, 0)]
def png(pixels, size):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y*size*4:(y+1)*size*4] for y in range(size))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
for name in ('powermac_g4', 'quadra_800', 'powermac_7500'):
    data = (root / f'Resources/Iconfactory/{name}.icns').read_bytes()
    elements = {}; at = 8
    while at < len(data):
        kind, size = struct.unpack_from('>4sI', data, at)
        elements[kind] = data[at+8:at+size]; at += size
    for size, rgb, mask, eight, bits in [(32, b'il32', b'l8mk', b'icl8', b'ICN#'), (16, b'is32', b's8mk', b'ics8', b'ics#')]:
        count = size * size
        pixels = bytearray(count * 4)
        if rgb in elements and mask in elements:
            source = elements[rgb]; planes = []; at = 0
            # 32px il32 has no reserved prefix; 128px it32 is not exported.
            for plane in range(3):
                values = bytearray()
                while len(values) < count:
                    control = source[at]; at += 1
                    if control >= 128:
                        n = control - 125; values.extend(bytes([source[at]]) * n); at += 1
                    else:
                        n = control + 1; values.extend(source[at:at+n]); at += n
                assert len(values) == count
                planes.append(values)
            for i in range(count): pixels[i*4:i*4+4] = bytes([p[i] for p in planes] + [elements[mask][i]])
        else:
            colors = elements[eight]; masks = elements[bits][count//8:]
            for i in range(count): pixels[i*4:i*4+4] = bytes((*palette[colors[i]], 255 if masks[i//8] & (128 >> (i%8)) else 0))
        (out / f'{name}-{size}.png').write_bytes(png(pixels, size))

# Export ClassicMac's custom MIT Pippin machine icon from platinum/icons/machines.icons.
icon_palette = {}
for line in (root / 'platinum/icons/palette.txt').read_text().splitlines():
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    sym, hexv = line.split(None, 1)
    if len(hexv) == 6:
        r, g, b = int(hexv[0:2], 16), int(hexv[2:4], 16), int(hexv[4:6], 16)
        a = 255
    else:
        a, r, g, b = int(hexv[0:2], 16), int(hexv[2:4], 16), int(hexv[4:6], 16), int(hexv[6:8], 16)
    icon_palette[sym] = (r, g, b, a)
icon_palette['.'] = (0, 0, 0, 0)

lines = (root / 'platinum/icons/machines.icons').read_text().splitlines()
i = 0
while i < len(lines):
    parts = lines[i].split()
    if len(parts) == 3 and parts[0] == 'icon' and parts[1] == 'apple_pippin':
        sz = int(parts[2])
        rows = lines[i + 1:i + 1 + sz]
        pixels = bytearray()
        for row in rows:
            for ch in row[:sz]:
                pixels.extend(icon_palette[ch])
        (out / f'apple_pippin-{sz}.png').write_bytes(png(bytes(pixels), sz))
        i += 1 + sz
    else:
        i += 1
