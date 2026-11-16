# RetroBoot 2.0.2 Radeon cursor correction

ClassicMac ports the cursor correction from RetroBoot commit
[`88d074a55b00af7507e9412734ee83fd988876b3`](https://github.com/amcchord/RetroBoot/commit/88d074a55b00af7507e9412734ee83fd988876b3).
RetroBoot prepares its 2.0.2 release at `1c9a702`. ClassicMac's existing
2.0.1 engine import remains in place; this is a focused cursor update.

Mac OS 9 advances the cursor's VRAM address for clipped top rows. The old
renderer subtracted those rows and the horizontal offset a second time,
reading earlier VRAM. Near the top/left screen edges this could draw tiles
or incorrect cursor pixels. The correction preserves the selected first row
and shifts within both 64-bit monochrome planes. Cached clipping/mode state
and unlock invalidation also update correctly. Upstream's bounded color
cursor composition is retained with the required state fields.

`ati/retroboot-radeon-cursor.patch` extracts only the cursor-related hunks
from `emulator/patches/retroboot-macosx-agp.patch`. The full upstream AGP,
UniNorth and OS X platform changes are excluded. The patch changes just
`hw/display/ati.c` and `hw/display/ati_int.h` and is applied by the shared
source preparation used by macOS, Windows and Linux. CGL/WGL/EGL backends,
host resizing, game input, firmware and startup policy are preserved.
The [manifest](retroboot-2.0.2-cursor-import.json) records exact hashes.

The portable `ati/tests/test_radeon_cursor.py` adapts the upstream actual-device
test to loopback TCP, allowing the same pixel checks on Windows. It uses
synthetic VRAM and qtest with QMP screen captures, requiring no guest OS or
private ROM. Its 144 monochrome cases cover two depths, top offsets 0/1/63,
left offsets 0/1/7/8/31/63 and four positions including the right edge and
fully outside the screen. It also checks visible cursor disable/erasure,
color modes 2/3, transparency/alpha, left clipping and no next-row wrap.

The qualified old engine fails at the first left-clipped case: pixel (0,0)
is black where the independently generated cursor expects white. The rebuilt
Mac engine passes all cases and 598,030 exact RGB comparisons. The actual
Apple M5 Max CGL smoke test passes context, shader/link, framebuffer, triangle
and readback checks. Existing Mac engine/device/display gates also pass.
These are correctness checks; no new game speedup is claimed.

Full guest testing also reproduced the existing tablet/network interrupt
conflict on the Mac frontend, with the normal display-control device order.
The old engine's automatic slot 18 does not reach Finder within 65 seconds;
changing only the tablet to slot 19 reaches Finder in 24.44 seconds and shuts
down cleanly. An earlier diagnostic used a different device order and booted
with automatic allocation; it is retained as a diagnostic, not the control.
The shipping Swift launcher now uses the same conditional slot 19 allocation
already qualified on Windows/Linux. Sound, networking, Tools and tablet stay
enabled. The native argument oracle is now shared across all desktop hosts.
This startup correction is separate from the imported cursor patch.

The shared Mac build and both platform CI workflows now require the cursor
gate. Release validation and corresponding prepared source archives will
record the exact final packages. Current stable 4.6.3 remains available until
the new packages complete validation.

The new checkout also exposed two vendored printf files whose original CRLF
blobs conflict with the general C/H LF rule. Explicit raw-byte exceptions
preserve those MIT source files without changing their contents. Copland's
existing byte exceptions remain intact.
