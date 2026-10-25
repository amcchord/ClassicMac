#!/usr/bin/env python3
"""Check captured Platinum title bars and matching real desktop pixels.

Requires Pillow. Input is a --qualify output tree containing PNG/JSON captures.
This checks square, uniformly repeated UI pixels rather than comparing the
renderer against itself. --windows-matrix requires all five real OS DPI scales.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--windows-matrix', action='store_true')
parser.add_argument('--output', type=Path)
a = parser.parse_args()
rows = []
for meta in sorted(a.root.rglob('*.json')):
    capture = meta.with_suffix('.png')
    if not capture.exists() or meta.name == 'result.json':
        continue
    state = json.loads(meta.read_text())
    if 'scale' not in state:
        continue
    zoom = state['scale']
    assert zoom >= 1 and abs(zoom - round(zoom)) < .001, (meta, 'fractional UI zoom', zoom)
    zoom = round(zoom)
    with Image.open(capture) as source:
        image = source.convert('RGB')
    assert image.width % zoom == 0 and image.height % zoom == 0, (meta, 'partial UI pixel at window edge')
    title = image.crop((0, 0, image.width, 22 * zoom))
    base = title.resize((title.width // zoom, title.height // zoom), Image.Resampling.NEAREST)
    repeated = base.resize(title.size, Image.Resampling.NEAREST)
    assert ImageChops.difference(title, repeated).getbbox() is None, (meta, 'uneven title-bar pixel blocks')
    # Pure UI captures also cover text, buttons, fields, icons and scrollbars.
    # Running guest displays have their own explicitly selected pixel zoom.
    full_ui = not state.get('displays')
    if full_ui:
        base = image.resize((image.width // zoom, image.height // zoom), Image.Resampling.NEAREST)
        assert ImageChops.difference(image, base.resize(image.size, Image.Resampling.NEAREST)).getbbox() is None, (meta, 'uneven control/text pixel blocks')
    desktop = meta.with_name(meta.stem + '-desktop.png')
    actual_desktop = desktop.exists()
    if actual_desktop:
        with Image.open(desktop) as source:
            screen = source.convert('RGB')
        assert screen.size == image.size, (meta, 'desktop/window size differs', screen.size, image.size)
        assert ImageChops.difference(screen, image).getbbox() is None, (meta, 'desktop compositor changed UI pixels')
    rows.append({'capture': str(meta.relative_to(a.root)), 'windowsDpi': state.get('windowsDpi'),
                 'uiPixelZoom': zoom, 'uniformTitleBarPixels': True,
                 'uniformFullUI': True if full_ui else None,
                 'desktopMatches': True if actual_desktop else None})
assert rows, 'No captures were checked'
if a.windows_matrix:
    assert {r['windowsDpi'] for r in rows if r['desktopMatches']} >= {96, 120, 144, 168, 192}, 'Missing real Windows DPI desktop captures'
    assert {r['uiPixelZoom'] for r in rows} >= {1, 2}, 'Missing 1x or 2x UI'
report = {'captures': rows, 'uniformTitleBars': len(rows),
          'matchingDesktopCaptures': sum(r['desktopMatches'] is True for r in rows)}
if a.output:
    a.output.write_text(json.dumps(report, indent=2) + '\n')
print(f"{len(rows)} uniform title bars; {report['matchingDesktopCaptures']} exact desktop captures")
