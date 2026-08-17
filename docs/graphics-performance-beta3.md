# ClassicMac 1.7.0 beta 3 graphics performance

## Result

ClassicMac 1.7.0 beta 3 improves the MacBench 5 Graphics score from a
signed-beta baseline of 9,078 to 14,457 when running the exact QEMU binary
shipped in the notarized beta 3 application. That is a 59.25% increase.

The result is a 2D QuickDraw and framebuffer-path speedup. It is not a 3D
acceleration implementation: classic Mac OS applications that require a native
QuickDraw 3D, RAVE, or OpenGL hardware driver still use their existing software
paths.

The controlled candidate validation produced these additional results:

| Run | MacBench 5 Graphics score | Change from 9,078 |
| --- | ---: | ---: |
| Cold candidate | 13,484 | +48.54% |
| Warm candidate | 14,765 | +62.65% |
| Two-run candidate mean | 14,125 | +55.59% |
| Exact notarized release bundle | 14,457 | +59.25% |

The release-bundle run used the Power Mac profile with a 7400 CPU, 512 MiB of
RAM, a 1920 x 1080 framebuffer in Thousands of colors, and the same MacBench 5
disk and test workflow as the 9,078 baseline. The final measurement launched
the QEMU executable from inside `ClassicMac.app`, after code signing and before
delivery of the stapled DMG.

## Where the time was going

Classic QuickDraw performs many small CPU writes directly into the video
framebuffer. The stock emulation path made those writes expensive in several
independent ways:

1. VGA frequently rendered into a converted shadow surface instead of letting
   Cocoa display the guest's big-endian framebuffer directly.
2. QEMU's VGA dirty logger cleared its dirty bitmap after each display refresh.
   Re-arming dirty logging write-protected the framebuffer pages, so the first
   write to each page in the next frame entered TCG's `notdirty` slow path.
3. TCG write handling searched translation-block lists even when the write
   could not overlap any translated code on the page.
4. Clang expanded QEMU's small victim-TLB scan into a large sequence of
   outlined helper calls on Apple Silicon.
5. The guest software cursor modified framebuffer pixels whenever the pointer
   moved, adding more guest writes, dirty pages, and scanout work.
6. A 32-bit framebuffer moved twice as much pixel data as the 15-bit Thousands
   mode that is sufficient for most classic Mac OS software.

The beta addresses each cost at the layer where it originates.

## Direct big-endian scanout

`cocoaui/display-performance.patch` teaches the Cocoa display backend to
accept QEMU's big-endian 32-bit framebuffer format and describe its byte order
correctly to Core Graphics. `ppcvid/vga-fast-scanout.patch` extends that path to
the big-endian RGB555/RGB565 layouts used by the classic Thousands mode.

When Cocoa accepts one of those layouts, VGA shares the framebuffer surface
directly instead of converting every pixel into a host-endian shadow buffer.
The Cocoa display listener refreshes at 17 ms, close to the classic Mac's 60 Hz
vertical-blank cadence, so direct scanout remains responsive without polling
far more often than the guest can produce a frame.

The setting is conservative. If the active display format is unsupported or a
shadow surface is otherwise required, VGA continues to use its original
conversion path.

## Avoiding repeated framebuffer write traps

The new opt-in `VGA.untracked-vram` property disables VGA dirty logging only
while direct shared scanout is active. Cocoa is already reading the live VRAM
surface, so a per-page dirty snapshot is unnecessary; the display backend is
notified once per refresh instead.

This removes a particularly costly cycle from framebuffer-heavy workloads:

1. clear the VGA dirty bitmap after a refresh;
2. write-protect the framebuffer pages again;
3. trap the first guest write to every page through TCG;
4. mark the page dirty and resume execution;
5. repeat at the next refresh.

Dirty logging starts again automatically if VGA returns to a shadow surface.
The lower indexed-color modes also retain the tracked path because they still
need conversion. This keeps the optimization local to formats for which Cocoa
can safely consume live VRAM.

## Faster translated-code invalidation checks

`powermac/tcg-graphics-fast-path.patch` adds a conservative 32-region coverage
summary to each TCG `PageDesc`. Whenever a translation block is attached to a
page, the summary records every page region that might contain translated
bytes. A same-page write whose regions do not intersect that summary can return
immediately without walking the page's translation-block list.

For a possible overlap, the fast path locks the page and attempts a one-pass
invalidation. It deliberately falls back to QEMU's original page-collection
path for cross-page writes, translation blocks spanning pages, and any case
whose safety is ambiguous. The coverage bits are only cleared when the page no
longer contains translation blocks, so the summary can produce false positives
but not unsafe false negatives.

The summary occupies padding that was already present in `PageDesc`; a build
assertion preserves the original 16-byte structure size. Current-translation-
block invalidation also preserves QEMU's restore-and-exit behavior.

The same patch disables compiler unrolling for the fixed-size victim-TLB scan.
The scan's order and semantics are unchanged, but Clang no longer turns it into
hundreds of outlined calls in the arm64 host binary.

## Host-composited cursor

`ppcvid/vga-hardware-cursor.patch` adds an opt-in QEXT channel for a 16 x 16
ARGB cursor. The bundled PowerPC NDRV uses VideoServicesLib to prepare the
cursor image, uploads it once, and then sends position and visibility updates
through QEXT. QEMU hands those updates to the display frontend, allowing Cocoa
to composite the cursor without changing guest VRAM.

This keeps routine pointer motion out of the QuickDraw framebuffer and scanout
pipeline. The feature is advertised through a capability bit: older drivers
ignore it and retain their software cursor, while the new driver falls back if
the host does not expose the channel.

## CPU and color-depth choices

New Power Mac virtual machines use QEMU's PowerPC 7400 model by default. It is
intended for Mac OS 8.6 and 9, where the G4 model improves PowerPC-native and
mixed 68K/PowerPC workloads, including portions of the graphics benchmark. Mac
OS 8.5 predates the G4, so the app exposes a compatibility toggle that selects
the original G3 model. Existing VM packages without the new setting decode as
G3 to avoid silently changing their virtual CPU.

Power Mac machines can boot in Thousands or Millions of colors. Thousands is
the default because its 15-bit framebuffer halves pixel bandwidth relative to
32-bit Millions while retaining direct-color output. Users can still choose
Millions when color precision matters more than throughput.

## Reproducibility and validation

The QEMU build script applies all four performance patches in a fixed order and
verifies that the PowerPC binary exposes the `hardware-cursor`,
`untracked-vram`, and packed-low-depth VGA properties. The application tests
cover the G3/G4 selection, Power Mac depth normalization, boot-depth arguments,
and the new VGA options.

Before beta delivery:

- all 25 Swift tests passed;
- the QEMU build and patch reversibility checks passed;
- the exact bundled binary produced the 14,457 MacBench result;
- both the application and DMG were accepted by Apple's notarization service;
- both tickets were stapled and validated;
- Gatekeeper accepted the mounted application and the final DMG; and
- `hdiutil verify` accepted the final disk image.
