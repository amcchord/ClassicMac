"""Cursor template extraction + masked matching for closed-loop pointing.

The arrow cursor is captured once as a masked sprite (the pixels that change
when only the cursor moves). Thereafter it is located on any frame by scanning
for the position where every masked pixel matches. Positioning is closed-loop:
the caller locates the cursor, sends a relative nudge proportional to the
remaining delta, and repeats, which cancels out the guest's pointer
acceleration.
"""

import json
from vision import read_ppm


def build_template(frame_ppm, ref_ppm, out_json):
    """Extract the cursor sprite as (dx,dy,r,g,b) samples: pixels in `frame`
    that differ from `ref` and form the rightmost/topmost blob (the moved
    cursor). The tip is the blob's top-left, which is the arrow hotspot."""
    w, h, fp = read_ppm(frame_ppm)
    _, _, rp = read_ppm(ref_ppm)
    diff = []
    for y in range(h):
        base = y * w * 3
        for x in range(w):
            o = base + x * 3
            if fp[o] != rp[o] or fp[o+1] != rp[o+1] or fp[o+2] != rp[o+2]:
                diff.append((x, y))
    if not diff:
        raise RuntimeError("no diff pixels; cursor did not move")
    # The cursor occupies a compact ~16x16 region; the moved cursor is the
    # blob at the largest x. Cluster by taking pixels within 20px of max x.
    maxx = max(p[0] for p in diff)
    blob = [p for p in diff if p[0] >= maxx - 18]
    minx = min(p[0] for p in blob)
    miny = min(p[1] for p in blob)
    # Keep near-black pixels (the arrow's solid body) and near-white pixels
    # (its opaque outline). Both are background-independent; requiring both
    # patterns prevents false matches on solid dark regions.
    dark, white = [], []
    for (x, y) in blob:
        o = (y * w + x) * 3
        r, g, b = fp[o], fp[o+1], fp[o+2]
        if r < 70 and g < 70 and b < 70:
            dark.append([x - minx, y - miny])
        elif r > 200 and g > 200 and b > 200:
            white.append([x - minx, y - miny])
    with open(out_json, "w") as fh:
        json.dump({"tip": [minx, miny], "dark": dark, "white": white}, fh)
    return minx, miny, len(dark), len(white)


class Cursor:
    def __init__(self, template_json):
        with open(template_json) as fh:
            t = json.load(fh)
        self.dark = [tuple(p) for p in t["dark"]]
        self.white = [tuple(p) for p in t["white"]]
        pts = self.dark + self.white
        # Bounding size of the sprite for search bounds.
        self.sw = max(p[0] for p in pts) + 1
        self.sh = max(p[1] for p in pts) + 1

    def locate(self, frame_ppm, near=None, radius=None, tol=40):
        """Return (tipx, tipy) of the best match, or None. If `near` (x,y) and
        `radius` given, only search that window (fast path for tracking)."""
        w, h, pix = read_ppm(frame_ppm)
        if near and radius:
            x0 = max(0, near[0] - radius)
            y0 = max(0, near[1] - radius)
            x1 = min(w - self.sw, near[0] + radius)
            y1 = min(h - self.sh, near[1] + radius)
        else:
            x0, y0, x1, y1 = 0, 0, w - self.sw, h - self.sh
        best = None
        best_bad = 10**9
        limit = max(2, (len(self.dark) + len(self.white)) // 10)
        for ty in range(y0, y1 + 1):
            for tx in range(x0, x1 + 1):
                bad = 0
                ok = True
                for (dx, dy) in self.dark:
                    o = ((ty + dy) * w + (tx + dx)) * 3
                    if pix[o] > tol or pix[o+1] > tol or pix[o+2] > tol:
                        bad += 1
                        if bad > limit:
                            ok = False
                            break
                if ok:
                    for (dx, dy) in self.white:
                        o = ((ty + dy) * w + (tx + dx)) * 3
                        if (pix[o] < 255 - tol or pix[o+1] < 255 - tol
                                or pix[o+2] < 255 - tol):
                            bad += 1
                            if bad > limit:
                                ok = False
                                break
                if ok and bad < best_bad:
                    best_bad = bad
                    best = (tx, ty)
                    if bad == 0:
                        return best
        return best
