#!/usr/bin/env python3
"""Generate PlatinumKit's embedded assets from the MIT Platinum sources.

Inputs (all original MIT pixel art, see platinum/README.md):
  platinum/fonts/*.bdf          bitmap strikes
  platinum/icons/palette.txt    one letter per colour
  platinum/icons/*.icons        "icon <name> <size>" grids, one letter per pixel

Output:
  app/Sources/PlatinumKit/Generated/PlatinumAssets.swift

Each bitmap strike becomes a TrueType font whose glyphs are unions of pixel
squares (64 font units per pixel). At a point size equal to the strike's pixel
size every Mac pixel is exactly one point, so Core Text draws the text crisply
on Retina (2 device pixels per Mac pixel) and 1x displays alike, and SwiftUI's
Text and TextField can use the fonts directly. Regular and Small also get a
QuickDraw-style synthetic bold (every row ORed one pixel right, advance + 1).

Standard library only, so the generated Swift file can be rebuilt anywhere:
    python3 platinum/tools/build_platinum_swift.py
"""
from __future__ import annotations

import base64
import pathlib
import struct
import sys
import calendar

ROOT = pathlib.Path(__file__).resolve().parents[2]
FONTS = ROOT / "platinum" / "fonts"
ICONS = ROOT / "platinum" / "icons"
OUT = ROOT / "app" / "Sources" / "PlatinumKit" / "Generated" / "PlatinumAssets.swift"

UNITS = 64  # font units per Mac pixel


# ---------------------------------------------------------------- BDF strikes

class Glyph:
    def __init__(self, code: int, advance: int, rows: list[list[int]], left: int, top: int):
        self.code = code
        self.advance = advance
        self.rows = rows      # rows[0] is the top row; 1 = ink
        self.left = left      # x of column 0 relative to the pen
        self.top = top        # y of row 0 above the baseline (row 0 sits at top-1 .. top)


class Strike:
    def __init__(self, family: str, size: int, ascent: int, descent: int,
                 cap: int, xheight: int, glyphs: dict[int, Glyph]):
        self.family = family
        self.size = size
        self.ascent = ascent
        self.descent = descent
        self.cap = cap
        self.xheight = xheight
        self.glyphs = glyphs


def parse_bdf(path: pathlib.Path) -> Strike:
    props: dict[str, str] = {}
    glyphs: dict[int, Glyph] = {}
    lines = path.read_text(encoding="ascii").splitlines()
    i = 0
    size = 12
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("SIZE "):
            size = int(line.split()[1])
        elif line == "STARTPROPERTIES" or line.startswith("STARTPROPERTIES "):
            i += 1
            while lines[i].strip() != "ENDPROPERTIES":
                key, _, value = lines[i].strip().partition(" ")
                props[key] = value.strip().strip('"')
                i += 1
        elif line.startswith("STARTCHAR"):
            code = advance = 0
            bbx = (0, 0, 0, 0)
            rows: list[list[int]] = []
            i += 1
            while not lines[i].startswith("ENDCHAR"):
                part = lines[i].split()
                if part[0] == "ENCODING":
                    code = int(part[1])
                elif part[0] == "DWIDTH":
                    advance = int(part[1])
                elif part[0] == "BBX":
                    bbx = tuple(int(v) for v in part[1:5])  # type: ignore[assignment]
                elif part[0] == "BITMAP":
                    w, h = bbx[0], bbx[1]
                    for r in range(h):
                        i += 1
                        bits = int(lines[i].strip(), 16)
                        nbits = len(lines[i].strip()) * 4
                        rows.append([(bits >> (nbits - 1 - c)) & 1 for c in range(w)])
                i += 1
            w, h, xoff, yoff = bbx
            glyphs[code] = Glyph(code, advance, rows, xoff, yoff + h)
        i += 1
    return Strike(props.get("FAMILY_NAME", path.stem), size,
                  int(props["FONT_ASCENT"]), int(props["FONT_DESCENT"]),
                  int(props.get("CAP_HEIGHT", "9")), int(props.get("X_HEIGHT", "7")), glyphs)


def embolden(strike: Strike) -> Strike:
    """QuickDraw's synthetic bold: each row ORed with itself one pixel right."""
    out: dict[int, Glyph] = {}
    for code, g in strike.glyphs.items():
        rows = []
        for row in g.rows:
            wide = row + [0]
            rows.append([wide[x] | (row[x - 1] if x > 0 else 0) for x in range(len(wide))])
        out[code] = Glyph(code, g.advance + 1, rows, g.left, g.top)
    return Strike(strike.family, strike.size, strike.ascent, strike.descent,
                  strike.cap, strike.xheight, out)


# ------------------------------------------------------------ pixel outlines

def contours(pixels: set[tuple[int, int]]) -> list[list[tuple[int, int]]]:
    """Boundary loops around a set of pixels (x right, y down).

    Every pixel contributes its four edges clockwise on screen; an edge shared
    by two pixels cancels against its reverse, leaving only the outline.
    Following the surviving edges gives closed loops: outer boundaries run
    clockwise on screen and holes counter-clockwise. With y flipped up for
    TrueType that becomes counter-clockwise outer loops, so the loops are
    reversed before they are written (TrueType wants clockwise outers).
    """
    edges: set[tuple[tuple[int, int], tuple[int, int]]] = set()
    for (x, y) in pixels:
        corners = [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]
        for k in range(4):
            a, b = corners[k], corners[(k + 1) % 4]
            if (b, a) in edges:
                edges.remove((b, a))
            else:
                edges.add((a, b))
    outgoing: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for a, b in edges:
        outgoing.setdefault(a, []).append(b)
    loops = []
    while outgoing:
        start = min(outgoing)
        loop = [start]
        cur = start
        heading = None
        while True:
            outs = outgoing[cur]
            if len(outs) == 1:
                nxt = outs.pop()
            else:
                # Two pixels touching only at a corner: turn right on screen so
                # each pixel keeps its own loop instead of a figure eight.
                def right_turn(b: tuple[int, int]) -> int:
                    d = (b[0] - cur[0], b[1] - cur[1])
                    if heading is None:
                        return 0
                    return -(heading[0] * d[1] - heading[1] * d[0])
                outs.sort(key=right_turn)
                nxt = outs.pop(0)
            if not outs:
                del outgoing[cur]
            heading = (nxt[0] - cur[0], nxt[1] - cur[1])
            cur = nxt
            if cur == start:
                break
            loop.append(cur)
        corners_only = []
        n = len(loop)
        for k in range(n):
            p0, p1, p2 = loop[k - 1], loop[k], loop[(k + 1) % n]
            if (p1[0] - p0[0]) * (p2[1] - p1[1]) - (p1[1] - p0[1]) * (p2[0] - p1[0]) != 0:
                corners_only.append(p1)
        loops.append(corners_only)
    return loops


def glyf_record(g: Glyph) -> tuple[bytes, tuple[int, int, int, int], int]:
    pixels = {(x, y) for y, row in enumerate(g.rows) for x, v in enumerate(row) if v}
    loops = contours(pixels)
    if not loops:
        return b"", (0, 0, 0, 0), 0
    points: list[tuple[int, int]] = []
    ends: list[int] = []
    for loop in loops:
        # Screen-clockwise becomes counter-clockwise with y up; reverse it.
        for (x, y) in reversed(loop):
            points.append(((g.left + x) * UNITS, (g.top - y) * UNITS))
        ends.append(len(points) - 1)
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    bbox = (min(xs), min(ys), max(xs), max(ys))
    data = struct.pack(">hhhhh", len(ends), *bbox)
    data += b"".join(struct.pack(">H", e) for e in ends)
    data += struct.pack(">H", 0)
    flags = bytearray()
    xdata = bytearray()
    ydata = bytearray()
    px = py = 0
    for (x, y) in points:
        flags.append(0x01)  # on-curve, 16-bit deltas
        xdata += struct.pack(">h", x - px)
        ydata += struct.pack(">h", y - py)
        px, py = x, y
    data += bytes(flags) + bytes(xdata) + bytes(ydata)
    if len(data) % 2:
        data += b"\0"
    return data, bbox, len(points)


# ------------------------------------------------------------- TrueType file

def checksum(data: bytes) -> int:
    data = data + b"\0" * (-len(data) % 4)
    return sum(struct.unpack(f">{len(data) // 4}I", data)) & 0xFFFFFFFF


def name_table(family: str, style: str, postscript: str, version: str) -> bytes:
    full = f"{family} {style}" if style != "Regular" else family
    names = {
        0: "Copyright (c) 2026 RetroBoot contributors. MIT License.",
        1: family,
        2: style,
        3: f"ClassicMac:{postscript}:{version}",
        4: full,
        5: f"Version {version}",
        6: postscript,
    }
    records = []
    strings = b""
    for nid, text in sorted(names.items()):
        raw = text.encode("utf-16-be")
        records.append(struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(raw), len(strings)))
        strings += raw
    header = struct.pack(">HHH", 0, len(records), 6 + 12 * len(records))
    return header + b"".join(records) + strings


def cmap_table(char_to_gid: dict[int, int]) -> bytes:
    codes = sorted(c for c in char_to_gid if c <= 0xFFFF)
    segments: list[tuple[int, int]] = []
    for c in codes:
        if segments and c == segments[-1][1] + 1:
            segments[-1] = (segments[-1][0], c)
        else:
            segments.append((c, c))
    segments.append((0xFFFF, 0xFFFF))
    seg_count = len(segments)
    ends = b"".join(struct.pack(">H", e) for _, e in segments)
    starts = b"".join(struct.pack(">H", s) for s, _ in segments)
    deltas = b""
    offsets = b""
    glyph_ids = b""
    for k, (s, e) in enumerate(segments):
        if s == 0xFFFF:
            deltas += struct.pack(">h", 1)
            offsets += struct.pack(">H", 0)
            continue
        # Glyph ids are contiguous within a segment only when the codes map in
        # order; use an idRangeOffset array so any mapping works.
        offset = 2 * (seg_count - k) + len(glyph_ids)
        deltas += struct.pack(">h", 0)
        offsets += struct.pack(">H", offset)
        for c in range(s, e + 1):
            glyph_ids += struct.pack(">H", char_to_gid[c])
    search = 1
    selector = 0
    while search * 2 <= seg_count:
        search *= 2
        selector += 1
    search *= 2
    sub = struct.pack(">HHHHHHH", 4, 0, 0, seg_count * 2, search, selector, seg_count * 2 - search)
    sub += ends + struct.pack(">H", 0) + starts + deltas + offsets + glyph_ids
    sub = sub[:2] + struct.pack(">H", len(sub)) + sub[4:]
    header = struct.pack(">HH", 0, 2)
    header += struct.pack(">HHI", 0, 3, 4 + 8 * 2)
    header += struct.pack(">HHI", 3, 1, 4 + 8 * 2)
    return header + sub


def build_ttf(strike: Strike, family: str, style: str, postscript: str, weight: int,
              version: str = "1.000") -> bytes:
    upem = strike.size * UNITS
    ascent = strike.ascent * UNITS
    descent = strike.descent * UNITS
    codes = sorted(strike.glyphs)
    order: list[int | None] = [None] + [c for c in codes]
    records = []
    advances = []
    lsbs = []
    extents = []          # (lsb, xMax) for glyphs with outlines
    max_points = max_contours = 0
    xmin = ymin = 10 ** 9
    xmax = ymax = -10 ** 9
    char_to_gid: dict[int, int] = {}
    replacement = strike.glyphs.get(0xFFFD) or strike.glyphs.get(ord("?"))
    for gid, code in enumerate(order):
        g = replacement if code is None else strike.glyphs[code]
        assert g is not None
        data, bbox, npoints = glyf_record(g)
        records.append(data)
        advance = g.advance * UNITS
        advances.append(advance)
        lsbs.append(bbox[0] if data else 0)
        if data:
            extents.append((advance, bbox[0], bbox[2]))
            max_points = max(max_points, npoints)
            max_contours = max(max_contours, struct.unpack(">h", data[:2])[0])
            xmin, ymin = min(xmin, bbox[0]), min(ymin, bbox[1])
            xmax, ymax = max(xmax, bbox[2]), max(ymax, bbox[3])
        if code is not None:
            char_to_gid[code] = gid
    num_glyphs = len(order)

    glyf = b""
    loca = []
    for data in records:
        loca.append(len(glyf))
        glyf += data
    loca.append(len(glyf))
    loca_table = b"".join(struct.pack(">I", v) for v in loca)

    mac_epoch = 2082844800
    stamp = calendar.timegm((2026, 10, 17, 0, 0, 0)) + mac_epoch
    head = struct.pack(">IIII", 0x00010000, 0x00010000, 0, 0x5F0F3CF5)
    head += struct.pack(">HH", 0b1011, upem)
    head += struct.pack(">qq", stamp, stamp)
    head += struct.pack(">hhhh", xmin, ymin, xmax, ymax)
    head += struct.pack(">HH", 1 if weight >= 700 else 0, strike.size)
    head += struct.pack(">hhh", 2, 1, 0)

    min_lsb = min(lsb for _, lsb, _ in extents)
    min_rsb = min(adv - right for adv, _, right in extents)
    max_extent = max(right for _, _, right in extents)
    hhea = struct.pack(">I", 0x00010000)
    hhea += struct.pack(">hhh", ascent, -descent, 0)
    hhea += struct.pack(">H", max(advances))
    hhea += struct.pack(">hhh", min_lsb, min_rsb, max_extent)
    hhea += struct.pack(">hhh", 1, 0, 0)          # caret slope rise/run, offset
    hhea += struct.pack(">hhhh", 0, 0, 0, 0)      # reserved
    hhea += struct.pack(">hH", 0, num_glyphs)

    maxp = struct.pack(">IHHH", 0x00010000, num_glyphs, max_points, max_contours)
    maxp += struct.pack(">HHHHHHHHHHH", 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)

    hmtx = b"".join(struct.pack(">Hh", a, l) for a, l in zip(advances, lsbs))

    printable = [a for c, a in zip(order, advances) if c is not None and 0x20 <= c < 0x7F]
    avg = sum(printable) // max(1, len(printable))
    fs_selection = (0x20 if weight >= 700 else 0x40) | 0x80  # bold/regular + USE_TYPO_METRICS
    os2 = struct.pack(">HhHHH", 4, avg, weight, 5, 0)
    os2 += struct.pack(">hhhh", upem // 2, upem // 2, 0, upem // 8)    # subscript
    os2 += struct.pack(">hhhh", upem // 2, upem // 2, 0, upem // 3)    # superscript
    os2 += struct.pack(">hhh", UNITS, (strike.xheight * UNITS) // 2, 0)  # strikeout, family class
    os2 += bytes(10)                                     # panose
    os2 += struct.pack(">IIII", 0x00000003, 0, 0, 0)     # Basic Latin + Latin-1
    os2 += b"PLAT"
    os2 += struct.pack(">HHH", fs_selection, min(char_to_gid), min(0xFFFF, max(char_to_gid)))
    os2 += struct.pack(">hhhHH", ascent, -descent, 0, ascent, descent)
    os2 += struct.pack(">II", 0x00000001, 0)
    os2 += struct.pack(">hhHHH", strike.xheight * UNITS, strike.cap * UNITS, 0, 0x20, 1)

    post = struct.pack(">IIhhIIIII", 0x00030000, 0, -UNITS, UNITS, 0, 0, 0, 0, 0)

    tables = {
        "OS/2": os2,
        "cmap": cmap_table(char_to_gid),
        "glyf": glyf,
        "head": head,
        "hhea": hhea,
        "hmtx": hmtx,
        "loca": loca_table,
        "maxp": maxp,
        "name": name_table(family, style, postscript, version),
        "post": post,
    }
    return assemble(tables)


def assemble(tables: dict[str, bytes]) -> bytes:
    tags = sorted(tables)
    count = len(tags)
    search = 1
    selector = 0
    while search * 2 <= count:
        search *= 2
        selector += 1
    header = struct.pack(">IHHHH", 0x00010000, count, search * 16, selector, count * 16 - search * 16)
    offset = 12 + 16 * count
    directory = b""
    body = b""
    head_offset = 0
    for tag in tags:
        data = tables[tag]
        if tag == "head":
            head_offset = offset + len(body)
        directory += struct.pack(">4sIII", tag.encode("ascii"), checksum(data), offset + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    font = bytearray(header + directory + body)
    adjust = (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF
    font[head_offset + 8:head_offset + 12] = struct.pack(">I", adjust)
    return bytes(font)


# ------------------------------------------------------------------- icons

def read_palette() -> dict[str, str]:
    palette: dict[str, str] = {}
    for line in (ICONS / "palette.txt").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        letter, value = line.split()
        palette[letter] = value.upper()
    return palette


def read_icons() -> list[tuple[str, int, list[str]]]:
    icons = []
    for path in sorted(ICONS.glob("*.icons")):
        lines = path.read_text().splitlines()
        k = 0
        while k < len(lines):
            line = lines[k].strip()
            if line.startswith("icon "):
                parts = line.split()
                name, size = parts[1], int(parts[2])
                rows = [lines[k + 1 + r].rstrip() for r in range(size)]
                for r in rows:
                    if len(r) != size:
                        raise SystemExit(f"{path.name}: {name} {size} has a {len(r)}-pixel row")
                icons.append((name, size, rows))
                k += size
            k += 1
    return icons


# ------------------------------------------------------------------- Swift

FONT_SPECS = [
    # (bdf file, family, style, postscript name, weight, embolden)
    ("RetroBoot-System-12.bdf", "Platinum System", "Regular", "PlatinumSystem", 400, False),
    ("RetroBoot-Regular-12.bdf", "Platinum Regular", "Regular", "PlatinumRegular", 400, False),
    ("RetroBoot-Regular-12.bdf", "Platinum Regular", "Bold", "PlatinumRegular-Bold", 700, True),
    ("RetroBoot-Small-10.bdf", "Platinum Small", "Regular", "PlatinumSmall", 400, False),
    ("RetroBoot-Small-10.bdf", "Platinum Small", "Bold", "PlatinumSmall-Bold", 700, True),
]


def swift_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    out = []
    out.append("// Generated by platinum/tools/build_platinum_swift.py. Do not edit.")
    out.append("// Fonts and icons are original MIT pixel art; see platinum/README.md.")
    out.append("")
    out.append("enum PlatinumAssets {")
    out.append("    struct FontData {")
    out.append("        let postScriptName: String")
    out.append("        let pixelSize: Int")
    out.append("        let ascent: Int")
    out.append("        let descent: Int")
    out.append("        let capHeight: Int")
    out.append("        let base64: String")
    out.append("    }")
    out.append("")
    out.append("    static let fonts: [FontData] = [")
    for bdf, family, style, ps, weight, bold in FONT_SPECS:
        strike = parse_bdf(FONTS / bdf)
        if bold:
            strike = embolden(strike)
        ttf = build_ttf(strike, family, style, ps, weight)
        encoded = base64.b64encode(ttf).decode("ascii")
        out.append(f"        FontData(postScriptName: {swift_string(ps)}, pixelSize: {strike.size}, "
                   f"ascent: {strike.ascent}, descent: {strike.descent}, capHeight: {strike.cap},")
        out.append(f"                 base64: {swift_string(encoded)}),")
    out.append("    ]")
    out.append("")
    out.append("    static let palette: [Character: UInt32] = [")
    for letter, value in read_palette().items():
        if len(value) == 6:
            value = "FF" + value
        out.append(f"        {swift_string(letter)}: 0x{value},")
    out.append("    ]")
    out.append("")
    out.append("    // name@size -> rows, one palette letter per pixel, \".\" transparent.")
    out.append("    static let icons: [String: [String]] = [")
    for name, size, rows in read_icons():
        out.append(f"        \"{name}@{size}\": [")
        for row in rows:
            out.append(f"            {swift_string(row)},")
        out.append("        ],")
    out.append("    ]")
    out.append("}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
