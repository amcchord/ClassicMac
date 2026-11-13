#!/usr/bin/env python3
"""Measure bitmap glyphs and CharWidth from the independent QuickDraw atlas.

Requires Pillow. The reference utility's A/B/C pages draw glyphs without
smoothing and encode each advance as eight black/white pixels. This reads
screenshots, not font programs. See platinum/reference/README.md.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / 'app/Tests/ClassicMacTests/Fixtures/Platinum'
SPECS = [('system', 'charcoal12', 'System-12'),
         ('regular', 'geneva12', 'Regular-12'),
         ('small', 'geneva9', 'Small-10')]


def measure(image):
    glyphs = {}
    for byte in range(32, 256):
        i = byte - 32
        x, y = 70 + i % 16 * 32, 120 + i // 16 * 24
        pixels = []
        for yy in range(20):
            for xx in range(-4, 20):
                color = image.getpixel((x + xx, y + yy))
                if color not in [(0, 0, 0), (255, 255, 255)]:
                    raise ValueError('Reference is smoothed, clipped or not the font atlas')
                if color == (0, 0, 0):
                    pixels.append((xx, yy - 16))
        advance = sum((image.getpixel((x + 22 + bit, y + 21)) == (0, 0, 0)) << bit
                      for bit in range(8))
        if advance > 24:
            raise ValueError('Invalid encoded CharWidth')
        glyphs[ord(bytes([byte]).decode('mac_roman'))] = (advance, pixels)
    return glyphs


def bdf_block(code, advance, pixels, size):
    if pixels:
        left, right = min(x for x, y in pixels), max(x for x, y in pixels)
        top, bottom = min(y for x, y in pixels), max(y for x, y in pixels)
        width, height, down = right - left + 1, bottom - top + 1, -bottom - 1
        nbits = (width + 7) // 8 * 8
        rows = ['%0*X' % (nbits // 4, sum(1 << (nbits - 1 - (x - left))
                 for x, yy in pixels if yy == y)) for y in range(top, bottom + 1)]
    else:
        width = height = left = down = 0
        rows = []
    return (f'STARTCHAR uni{code:04X}\nENCODING {code}\n'
            f'SWIDTH {round(advance * 1000 / size)} 0\nDWIDTH {advance} 0\n'
            f'BBX {width} {height} {left} {down}\nBITMAP\n' +
            ''.join(row + '\n' for row in rows) + 'ENDCHAR\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('references', type=Path)
    parser.add_argument('--utility', type=Path, required=True)
    args = parser.parse_args()
    metadata = {'system': 'Mac OS 9.2.1', 'gestalt': '0x0921', 'depth': 32,
                'smoothing': False, 'method': 'QuickDraw DrawChar and CharWidth; reference.c A/B/C',
                'utilitySHA256': hashlib.sha256(args.utility.read_bytes()).hexdigest(),
                'atlasRect': [66, 120, 520, 336], 'cell': [32, 24],
                'pen': [4, 16], 'advanceBits': [26, 21],
                'codes': [ord(bytes([c]).decode('mac_roman')) for c in range(32, 256)],
                'fonts': {}}
    for name, reference, filename in SPECS:
        raw = (args.references / (reference + '.png')).read_bytes()
        image = Image.open(args.references / (reference + '.png')).convert('RGB')
        glyphs = measure(image)
        path = ROOT / 'platinum/fonts' / ('RetroBoot-' + filename + '.bdf')
        old = path.read_text()
        header = old[:old.index('CHARS ')]
        header = re.sub(r'^COMMENT .*\n', '', header, flags=re.M)
        header = header.replace('STARTFONT 2.1\n',
            'STARTFONT 2.1\nCOMMENT Bitmap recreation measured with QuickDraw, Mac OS 9.2.1.\n'
            'COMMENT Native glyph and CharWidth provenance: Fixtures/Platinum/native-fonts.json\n'
            'COMMENT Non-Mac-Roman fallback glyphs retain RetroBoot artwork.\n')
        size = int(re.search(r'^SIZE (\d+)', header, re.M)[1])
        blocks = {int(re.search(r'ENCODING (\d+)', block)[1]): block
                  for block in re.findall(r'STARTCHAR .*?ENDCHAR\n', old, re.S)}
        for code, (advance, pixels) in glyphs.items():
            blocks[code] = bdf_block(code, advance, pixels, size)
        path.write_text(header + f'CHARS {len(blocks)}\n' +
                        ''.join(blocks[k] for k in sorted(blocks)) + 'ENDFONT\n')
        atlas = image.crop((66, 120, 586, 456))
        atlas.save(FIXTURES / ('native-font-' + name + '.png'))
        metadata['fonts'][name] = {'sourceSHA256': hashlib.sha256(raw).hexdigest(),
                                 'bdfSHA256': hashlib.sha256(path.read_bytes()).hexdigest()}
        if name == 'system':
            ascii_atlas = Image.new('RGB', (256, 120), 'white')
            for code in range(32, 127):
                i = code - 32
                for x, y in glyphs[code][1]:
                    ascii_atlas.putpixel((i % 16 * 16 + x, i // 16 * 20 + 14 + y), (0, 0, 0))
            ascii_atlas.save(FIXTURES / 'system-ascii.png')
            (FIXTURES / 'system-advances.json').write_text(json.dumps(
                [glyphs[c][0] for c in range(32, 127)]) + '\n')
    (FIXTURES / 'native-fonts.json').write_text(json.dumps(metadata, indent=2) + '\n')


if __name__ == '__main__':
    main()
