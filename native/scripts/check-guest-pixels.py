#!/usr/bin/env python3
"""Compare every visible Sharp Pixels guest pixel with the raw RFB frame.

Requires Pillow. Input is a native --qualify capture directory. Fit Window
is reported separately because it explicitly permits fractional resampling.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
results = []
for meta in sorted(args.root.rglob('*.json')):
    raw = meta.with_name(meta.stem + '-guest.png')
    capture = meta.with_suffix('.png')
    if not raw.exists() or not capture.exists():
        continue
    state = json.loads(meta.read_text())
    if state.get('captureKind') != 'guest':
        continue
    assert len(state['displays']) == 1, 'Use one isolated guest per pixel recipe'
    view = state['displays'][0]
    if view['fitWindow']:
        results.append({'capture': meta.name, 'mode': 'Fit Window', 'compared': False})
        continue
    dpi = view['scale']
    width, height = round(view['imageWidth'] * dpi), round(view['imageHeight'] * dpi)
    zoom = width / view['frameWidth']
    assert zoom >= 1 and abs(zoom - round(zoom)) < .00001, (meta, 'fractional guest pixels')
    assert height == view['frameHeight'] * round(zoom), (meta, 'unequal X/Y pixel zoom')
    x = round((view['viewX'] + view['imageX']) * dpi)
    y = round((view['viewY'] + view['imageY']) * dpi)
    left = max(x, round(view['viewX'] * dpi))
    top = max(y, round(view['viewY'] * dpi))
    right = min(x + width, round((view['viewX'] + view['displayViewportWidth']) * dpi))
    bottom = min(y + height, round((view['viewY'] + view['displayViewportHeight']) * dpi))
    assert right > left and bottom > top, (meta, 'no visible guest pixels')
    expected = Image.open(raw).convert('RGB').resize((width, height), Image.Resampling.NEAREST)
    expected = expected.crop((left - x, top - y, right - x, bottom - y))
    actual = Image.open(capture).convert('RGB').crop((left, top, right, bottom))
    assert ImageChops.difference(expected, actual).getbbox() is None, (meta, 'guest pixels changed')
    results.append({'capture': meta.name, 'mode': 'Sharp Pixels', 'compared': True,
                    'zoom': round(zoom), 'pixelsCompared': expected.width * expected.height,
                    'differingPixels': 0})
assert any(r['compared'] for r in results), 'No Sharp Pixels guest capture was compared'
report = {'captures': results, 'pixelsCompared': sum(r.get('pixelsCompared', 0) for r in results)}
if args.output:
    args.output.write_text(json.dumps(report, indent=2) + '\n')
print(f"{sum(r['compared'] for r in results)} guest captures; {report['pixelsCompared']} pixels; zero differences")
