"""Tiny PPM (P6) reader + cursor locator for the OS 9 install harness.

No third-party deps. The guest applies pointer acceleration, so positioning is
done closed-loop: capture a frame, locate the arrow cursor, nudge, repeat.
The arrow cursor is located by template-matching a captured sprite (its tip is
the hotspot at the sprite's top-left).
"""

import struct


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:2] == b"P6", "not a P6 ppm"
    # Parse header: P6 <w> <h> <maxval>\n then binary.
    idx = 2
    vals = []
    while len(vals) < 3:
        # skip whitespace/comments
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if data[idx:idx+1] == b"#":
            while data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while data[idx] not in b" \t\r\n":
            idx += 1
        vals.append(int(data[start:idx]))
    w, h, maxv = vals
    idx += 1  # single whitespace after maxval
    pix = data[idx:idx + w * h * 3]
    return w, h, pix


def _px(pix, w, x, y):
    o = (y * w + x) * 3
    return pix[o], pix[o + 1], pix[o + 2]


def extract_cursor_sprite(frame_path, baseline_path, out_path,
                          box=24):
    """Diff a frame (cursor present) against a baseline where the cursor is
    elsewhere; the changed pixels bound the cursor. Saves the sprite region and
    returns (sprite_w, sprite_h, rgba-ish list) plus tip offset (0,0)."""
    w, h, fpix = read_ppm(frame_path)
    _, _, bpix = read_ppm(baseline_path)
    minx = miny = 10**9
    maxx = maxy = -1
    for y in range(h):
        row = y * w * 3
        for x in range(w):
            o = row + x * 3
            if (fpix[o] != bpix[o] or fpix[o+1] != bpix[o+1]
                    or fpix[o+2] != bpix[o+2]):
                if x < minx: minx = x
                if y < miny: miny = y
                if x > maxx: maxx = x
                if y > maxy: maxy = y
    if maxx < 0:
        return None
    return (minx, miny, maxx, maxy)
