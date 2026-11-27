#!/usr/bin/env python3
"""Draw ClassicMac's machine icons as palette grids (platinum/icons/machines.icons).

Original pixel art for ClassicMac (MIT), drawn from simple shapes in the
shared Platinum palette (platinum/icons/palette.txt) so each icon stays crisp
and consistent with the RetroBoot set. No Apple artwork is used.

    python3 platinum/tools/draw_machine_icons.py
    python3 platinum/tools/build_platinum_swift.py
"""
from __future__ import annotations

import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "platinum" / "icons" / "machines.icons"


class Grid:
    def __init__(self, size: int):
        self.size = size
        self.px = [["."] * size for _ in range(size)]

    def set(self, x: int, y: int, c: str) -> None:
        if 0 <= x < self.size and 0 <= y < self.size:
            self.px[y][x] = c

    def rect(self, x: int, y: int, w: int, h: int, c: str) -> None:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def frame(self, x: int, y: int, w: int, h: int, c: str) -> None:
        self.hline(x, y, w, c)
        self.hline(x, y + h - 1, w, c)
        self.vline(x, y, h, c)
        self.vline(x + w - 1, y, h, c)

    def hline(self, x: int, y: int, w: int, c: str) -> None:
        for xx in range(x, x + w):
            self.set(xx, y, c)

    def vline(self, x: int, y: int, h: int, c: str) -> None:
        for yy in range(y, y + h):
            self.set(x, yy, c)

    def bevel(self, x: int, y: int, w: int, h: int, light: str, dark: str) -> None:
        """Highlight on the top/left inside edge, shadow on the bottom/right."""
        self.hline(x, y, w - 1, light)
        self.vline(x, y, h - 1, light)
        self.hline(x + 1, y + h - 1, w - 1, dark)
        self.vline(x + w - 1, y + 1, h - 1, dark)

    def rows(self) -> list[str]:
        return ["".join(r) for r in self.px]


def g4_32() -> Grid:
    """Power Mac G4 (Graphite): a tower with the four corner handles."""
    g = Grid(32)
    # Handles above and below the case, at both sides.
    for hx in (7, 20):
        g.rect(hx, 1, 5, 3, "6")
        g.frame(hx, 1, 5, 4, "K")
        g.rect(hx + 1, 2, 3, 2, "B")
        g.hline(hx + 1, 2, 3, "D")
        g.rect(hx, 28, 5, 3, "6")
        g.frame(hx, 27, 5, 4, "K")
        g.rect(hx + 1, 28, 3, 2, "9")
    # The case.
    g.rect(5, 4, 22, 24, "A")
    g.frame(5, 4, 22, 24, "K")
    g.bevel(6, 5, 20, 22, "D", "6")
    # Graphite front panel with a lighter rim.
    g.rect(8, 6, 16, 20, "7")
    g.frame(8, 6, 16, 20, "5")
    g.hline(9, 7, 14, "8")
    # Optical drive and Zip bays.
    g.rect(10, 9, 12, 3, "9")
    g.hline(10, 9, 12, "B")
    g.hline(11, 10, 10, "K")
    g.rect(10, 13, 12, 2, "8")
    g.hline(10, 13, 12, "9")
    g.hline(12, 14, 8, "5")
    # Power button: a soft blue light.
    g.rect(14, 18, 4, 4, "6")
    g.frame(14, 18, 4, 4, "K")
    g.rect(15, 19, 2, 2, "L")
    g.set(15, 19, "W")
    # Speaker grille.
    for gx in range(11, 21, 2):
        g.set(gx, 23, "4")
        g.set(gx + 1, 24, "4")
    return g


def g4_16() -> Grid:
    g = Grid(16)
    for hx in (3, 10):
        g.frame(hx, 0, 3, 3, "K")
        g.set(hx + 1, 1, "D")
        g.frame(hx, 13, 3, 3, "K")
        g.set(hx + 1, 14, "9")
    g.rect(2, 2, 12, 12, "A")
    g.frame(2, 2, 12, 12, "K")
    g.hline(3, 3, 10, "D")
    g.vline(3, 3, 10, "D")
    g.rect(4, 4, 8, 9, "7")
    g.hline(5, 5, 6, "B")
    g.hline(5, 6, 6, "K")
    g.hline(5, 8, 6, "9")
    g.rect(7, 10, 2, 2, "L")
    g.set(7, 10, "W")
    return g


def quadra_32() -> Grid:
    """Macintosh Quadra 800: a beige mini tower with floppy and CD bays."""
    g = Grid(32)
    g.rect(7, 2, 18, 27, "h")
    g.frame(7, 2, 18, 27, "K")
    g.bevel(8, 3, 16, 25, "W", "j")
    # Front: a lower plinth and rounded corner shading.
    g.hline(8, 25, 16, "H")
    g.hline(8, 26, 16, "j")
    # CD-ROM bay with tray line and button.
    g.rect(10, 6, 12, 4, "H")
    g.frame(10, 6, 12, 4, "J")
    g.hline(11, 8, 10, "K")
    g.set(19, 7, "j")
    # Floppy slot.
    g.rect(11, 12, 10, 3, "H")
    g.hline(11, 13, 10, "K")
    g.set(20, 14, "J")
    # Blank bay seams.
    g.hline(10, 17, 12, "H")
    g.hline(10, 18, 12, "W")
    # Power LED and button.
    g.set(11, 22, "g")
    g.rect(18, 21, 3, 2, "H")
    g.frame(17, 20, 5, 4, "j")
    # Feet.
    g.hline(8, 29, 3, "K")
    g.hline(21, 29, 3, "K")
    return g


def quadra_16() -> Grid:
    g = Grid(16)
    g.rect(3, 1, 10, 14, "h")
    g.frame(3, 1, 10, 14, "K")
    g.hline(4, 2, 8, "W")
    g.vline(4, 2, 12, "W")
    g.hline(5, 4, 6, "J")
    g.hline(5, 5, 6, "K")
    g.hline(5, 7, 6, "K")
    g.set(5, 11, "g")
    g.hline(8, 11, 3, "j")
    g.hline(4, 13, 8, "j")
    return g


def pm7500_32() -> Grid:
    """Power Macintosh 7500: a beige desktop case with a monitor on top."""
    g = Grid(32)
    # Monitor.
    g.rect(6, 1, 20, 17, "h")
    g.frame(6, 1, 20, 17, "K")
    g.bevel(7, 2, 18, 15, "W", "j")
    g.rect(9, 4, 14, 10, "K")
    g.rect(10, 5, 12, 8, "s")
    g.hline(10, 5, 12, "a")
    g.rect(11, 7, 4, 3, "l")
    g.hline(9, 15, 3, "j")
    g.rect(13, 18, 6, 2, "j")
    g.hline(13, 18, 6, "J")
    # Desktop case.
    g.rect(3, 20, 26, 9, "h")
    g.frame(3, 20, 26, 9, "K")
    g.bevel(4, 21, 24, 7, "W", "j")
    g.rect(6, 23, 8, 2, "H")
    g.hline(6, 24, 8, "K")
    g.rect(17, 23, 7, 2, "H")
    g.hline(17, 24, 7, "K")
    g.set(26, 23, "g")
    g.hline(5, 29, 22, "J")
    return g


def pm7500_16() -> Grid:
    g = Grid(16)
    g.rect(3, 0, 10, 9, "h")
    g.frame(3, 0, 10, 9, "K")
    g.rect(5, 2, 6, 5, "s")
    g.hline(5, 2, 6, "a")
    g.rect(6, 9, 4, 1, "J")
    g.rect(1, 10, 14, 5, "h")
    g.frame(1, 10, 14, 5, "K")
    g.hline(2, 11, 12, "W")
    g.hline(3, 12, 4, "K")
    g.hline(9, 12, 3, "K")
    g.set(12, 12, "g")
    return g


def disc(g: Grid, cx: float, cy: float, r: float, hole: float, sheen: bool) -> None:
    """A compact disc: light silver face, black rim, gray hub and a lavender sheen."""
    def inside(x: int, y: int) -> bool:
        return (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r

    def in_hole(x: int, y: int) -> bool:
        return (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= hole * hole

    for y in range(g.size):
        for x in range(g.size):
            if not inside(x, y):
                continue
            edge = any(not inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if edge:
                g.set(x, y, "K")
            elif in_hole(x, y):
                hole_edge = any(not in_hole(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                g.set(x, y, "9" if hole_edge else ".")
            else:
                # Brighter toward the upper left, as if lit from there.
                t = (x - cx) + (y - cy)
                g.set(x, y, "W" if t < -r * 0.6 else "E" if t < r * 0.4 else "D")
    if sheen:
        for i, c in enumerate("lasd"):
            g.hline(int(cx - r * 0.55) + i, int(cy - r * 0.3) + i, 3, c)


def library_32() -> Grid:
    """The Mac OS Library: a CD with a download arrow."""
    g = Grid(32)
    disc(g, 13.5, 15.5, 12.5, 2.6, sheen=True)
    # Download arrow in the lower right.
    g.rect(23, 14, 5, 9, "b")
    g.frame(23, 14, 5, 9, "N")
    g.rect(24, 14, 3, 9, "L")
    for i in range(6):
        g.hline(20 + i, 22 + i, 11 - 2 * i, "b")
        g.set(20 + i, 22 + i, "N")
        g.set(30 - i, 22 + i, "N")
    g.set(25, 27, "N")
    return g


def library_16() -> Grid:
    g = Grid(16)
    disc(g, 7, 7.5, 6.6, 1.3, sheen=False)
    g.hline(3, 5, 2, "a")
    g.hline(4, 6, 2, "s")
    g.rect(12, 7, 2, 4, "b")
    for i in range(3):
        g.hline(10 + i, 10 + i, 6 - 2 * i, "b")
    return g


def pippin_32() -> Grid:
    """Apple / Bandai Pippin: sculpted console behind the iconic AppleJack controller."""
    g = Grid(32)
    # 1. Sculpted charcoal/graphite Pippin console chassis (y = 2..16).
    for row_idx, (x0, x1) in enumerate([
        (9, 22),   # y = 2 top outline
        (8, 23),   # y = 3
        (7, 24),   # y = 4
        (6, 25),   # y = 5
        (5, 26),   # y = 6
        (4, 27),   # y = 7
    ]):
        y = 2 + row_idx
        if row_idx == 0:
            g.hline(x0, y, x1 - x0 + 1, "K")
        else:
            g.hline(x0, y, x1 - x0 + 1, "8")
            g.set(x0, y, "K")
            g.set(x1, y, "K")
            g.set(x0 + 1, y, "B")
            g.set(x1 - 1, y, "5")
            if row_idx == 1:
                g.hline(x0 + 2, y, x1 - x0 - 3, "B")
            elif row_idx >= 3:
                # Top-loading CD-ROM recessed platter inset
                g.hline(10, y, 12, "5")
                g.set(10, y, "9")
                g.set(21, y, "4")
    g.hline(11, 4, 10, "K")
    g.hline(11, 6, 10, "4")

    # Main slanted front console body (y = 8..16)
    g.rect(3, 8, 26, 9, "7")
    g.frame(3, 8, 26, 9, "K")
    g.hline(4, 8, 24, "A")
    g.vline(4, 9, 7, "9")
    g.vline(27, 9, 7, "5")
    # Recessed front panel band with ADB port, emerald power LED, & louver vents
    g.rect(5, 10, 22, 5, "5")
    g.hline(6, 10, 20, "8")
    g.hline(5, 11, 22, "4")
    g.rect(6, 12, 3, 2, "3")
    g.set(6, 12, "K")
    g.set(10, 13, "g")
    g.set(11, 13, "G")
    for gx in range(15, 25, 2):
        g.set(gx, 12, "4")
        g.set(gx, 13, "4")
    g.hline(4, 15, 24, "5")
    g.hline(5, 16, 22, "K")

    # 2. Iconic boomerang AppleJack gamepad foreground (y = 17..30).
    controller_spans = [
        (17, [(8, 23)]),
        (18, [(6, 25)]),
        (19, [(4, 27)]),
        (20, [(3, 28)]),
        (21, [(3, 28)]),
        (22, [(3, 28)]),
        (23, [(3, 28)]),
        (24, [(3, 28)]),
        (25, [(3, 12), (19, 28)]),
        (26, [(3, 11), (20, 28)]),
        (27, [(4, 10), (21, 27)]),
        (28, [(5, 9), (22, 26)]),
    ]
    g.hline(12, 26, 8, "x")
    for y, spans in controller_spans:
        for sx, ex in spans:
            g.hline(sx, y, ex - sx + 1, "E")
            g.set(sx, y, "K")
            g.set(ex, y, "K")
    # Crisp controller outline + top highlight
    g.hline(8, 17, 16, "K")
    g.hline(6, 18, 3, "K")
    g.hline(23, 18, 3, "K")
    g.hline(4, 19, 3, "K")
    g.hline(25, 19, 3, "K")
    g.hline(9, 18, 14, "W")
    g.hline(7, 19, 18, "W")
    g.hline(4, 20, 4, "W")
    g.hline(24, 20, 4, "W")
    # Inner saddle & grip shading
    g.hline(12, 24, 8, "B")
    g.hline(12, 25, 8, "K")
    g.set(11, 25, "K")
    g.set(20, 25, "K")
    g.set(10, 26, "K")
    g.set(21, 26, "K")
    g.hline(5, 28, 5, "B")
    g.hline(22, 28, 5, "B")
    g.hline(5, 29, 4, "K")
    g.hline(23, 29, 4, "K")

    # Left: D-Pad cross (x=6..8, y=21..23)
    g.hline(6, 22, 3, "3")
    g.vline(7, 21, 3, "3")
    g.set(7, 22, "1")

    # Center: Glossy trackball (x=14..17, y=19..22) + Start/Select pips
    g.rect(14, 19, 4, 4, "4")
    g.frame(14, 19, 4, 4, "K")
    g.rect(15, 20, 2, 2, "6")
    g.set(15, 20, "W")
    g.set(12, 21, "7")
    g.set(19, 21, "7")

    # Right: Signature 4-color Pippin button diamond (Blue N, Yellow W, Green E, Red S)
    g.set(24, 20, "b")
    g.set(22, 22, "y")
    g.set(26, 22, "g")
    g.set(24, 24, "r")
    g.set(24, 21, "L")
    g.set(23, 22, "O")
    g.set(25, 22, "g")
    g.set(24, 23, "r")
    return g


def pippin_16() -> Grid:
    """16x16 Apple / Bandai Pippin console + AppleJack controller with 4-color diamond."""
    g = Grid(16)
    # Console behind (y = 0..7)
    g.hline(4, 0, 8, "K")
    g.hline(3, 1, 10, "8")
    g.set(3, 1, "K")
    g.set(12, 1, "K")
    g.hline(5, 1, 6, "B")
    g.rect(2, 2, 12, 6, "7")
    g.frame(2, 2, 12, 6, "K")
    g.hline(3, 3, 10, "9")
    g.hline(3, 4, 10, "5")
    g.set(4, 5, "g")  # emerald LED
    g.hline(7, 5, 5, "4")
    g.hline(3, 6, 10, "6")
    # AppleJack boomerang controller foreground (y = 8..15)
    g.hline(4, 8, 8, "K")
    for y, x0, x1 in [
        (9, 2, 13),
        (10, 1, 14),
        (11, 1, 14),
        (12, 1, 14),
    ]:
        g.hline(x0, y, x1 - x0 + 1, "E")
        g.set(x0, y, "K")
        g.set(x1, y, "K")
    g.hline(3, 9, 10, "W")
    g.rect(1, 13, 4, 2, "K")
    g.hline(2, 13, 2, "B")
    g.rect(11, 13, 4, 2, "K")
    g.hline(12, 13, 2, "B")
    g.hline(5, 13, 6, "K")
    # D-Pad left
    g.hline(2, 11, 3, "3")
    g.vline(3, 10, 3, "3")
    g.set(3, 11, "1")
    # Trackball center
    g.rect(7, 10, 2, 2, "4")
    g.set(7, 10, "A")
    # 4-color button diamond right
    g.set(12, 9, "b")   # Blue
    g.set(11, 11, "y")  # Yellow
    g.set(13, 11, "g")  # Green
    g.set(12, 12, "r")  # Red
    return g


ICONS = [
    ("powermac_g4", g4_32, g4_16, "Power Mac G4 (Graphite) tower"),
    ("quadra_800", quadra_32, quadra_16, "Macintosh Quadra 800 mini tower"),
    ("powermac_7500", pm7500_32, pm7500_16, "Power Macintosh 7500 desktop with monitor"),
    ("apple_pippin", pippin_32, pippin_16, "Apple / Bandai Pippin @World console and AppleJack controller"),
    ("library", library_32, library_16, "The Mac OS Library: a CD with a download arrow"),
]


def main() -> None:
    out = [
        "# ClassicMac machine and library icons.",
        "# Original pixel art for ClassicMac (MIT), generated by",
        "# platinum/tools/draw_machine_icons.py from simple shapes; no Apple artwork",
        "# was copied or traced. One character per pixel; \".\" is transparent.",
        "",
    ]
    for name, big, small, description in ICONS:
        for grid in (big(), small()):
            out.append(f"# {description}")
            out.append(f"icon {name} {grid.size}")
            out.extend(grid.rows())
            out.append("")
    OUT.write_text("\n".join(out))
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
