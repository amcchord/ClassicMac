# RetroBoot 2.0.1 import — November 12, 2026

ClassicMac imports the applicable emulator improvements from RetroBoot
`432a43d4ba8502a7afeaa5dd25ecf959c23aed1c`. That was RetroBoot `main` when
checked during this task. The earlier import was based on `1860881266`.
QEMU remains pinned to v11.0.2. Nothing was released or pushed.

[`retroboot-2.0.1-import.json`](retroboot-2.0.1-import.json) records exact
upstream/local paths, SHA-256s, adaptations and rebuilt firmware hashes.
There are 16 imported/updated engine patches: 15 verbatim and one adapted.
Two guest-driver changes are ported into ClassicMac's existing sources.

| Area | Imported behavior |
| --- | --- |
| TCG dirty stores | Avoid redundant not-dirty slow work; retire translations on repeatedly written data/code pages with backoff. |
| PowerPC | Split applicable 128-bit memory operations; implement the MSSCR0 cache-flush request used by guest software. Existing FPU/MMU/branch patches remain intact. |
| Standard VGA | Fast packed-row conversion; account for display flips occurring during a dirty snapshot; report damage from direct untracked VRAM writes. |
| ATI R100 | Updated readback/flip coordination, including settling a ready flip before clearing the former front buffer; stable virtual VBL scheduling and renderer work reduction. |
| Quadra reset/input | Exit with the guest-reset reason on a 68040 RESET; avoid redundant idle ADB input work; apply Timer 2 calibration hold on VIA1. |
| Enhanced Quadra framebuffer | Correct dirty lengths at each depth; align VBL with VIA1; inspect only visible dirty pages; bound/wrap dirty ranges; accept 32 as Millions in the depth register. |
| Power Mac video driver | Relative 16 ms pseudo-VBL replaces 30 ms. Resize debounce remains about 64 ms and pending timeout about ten seconds. |
| Quadra video driver | GetPage divides the framebuffer offset by height × rowbytes, including non-square modes. Existing bounded gray-row fill is retained. |
| Screamer | Add the strict pre-roll option. It remains off by default, as intended for desktop hosts. |

The shared VIA Timer 2 implementation and PPC translation hooks were already
present. `retroboot-q800-t2-hold.patch` reuses those hooks and removes the
generic timer hold's dependency on PPC coherent-timers; it enables the
hold on Quadra VIA1 without enabling PPC coherent timer policy on the Quadra.
The manifest marks this as adapted rather than byte-identical upstream code.

ClassicMac's CGL/OpenGL 4.1 overlay was rebased onto the new R100 renderer.
The larger upstream statistics buffer retains ClassicMac's renderer info.
The Windows WGL backend, 3D off switch, host resize service, game mouse
routing, native display transport and startup profiles are preserved.
Windows and Linux build scripts use the same shared preparation script, so
the new guest patches are not limited to the macOS build.

Source preparation now resets only tracked QEMU paths through a NUL-delimited
path list. Previously one copied, untracked device file could cause the
entire reset command to fail silently, making a second preparation unreliable.
Two consecutive preparations and a full build were verified after the import.

RetroBoot's appliance packaging, presenter services, boot-mode preference,
built-in-panel policy, product-specific OpenBIOS configuration and idle
extension installation were not brought into the desktop product. Its
qfb resize request and gray-row corrections overlap existing ClassicMac
fixes; the local resize retry path and bounded gray-row implementation remain.
The new GetPage correction is included. Existing startup patches retain their
documented local context rebases.

Both engines build on Apple Silicon. Device/firmware, ATI realization/CGL
off-switch, display shared-memory/reconnect, CPU, VIA, startup and OpenBIOS
gates pass. The Power Mac boots Mac OS 9.2.1 through the actual native
frontend; Quadra System 7.5.5 boots and restarts through both host and guest
reset paths. See the [UI audit](ui-fidelity-audit-2026-11.md) for the full
validation matrix and evidence.

The updated ATI engine also ran Quake III's intro, menu input, arena mouse
look/firing and return to Finder. The sound assertion failed because the
captured WAV contained silence. An isolated rebuild with the old ATI
renderer produced the same silent result and the same intro upload counts.
This qualifies the exercised drawing/input paths, not audio or a measured
performance gain. RetroBoot's hardware-specific benchmark numbers are not
presented as ClassicMac measurements.

New QEMU/guest-driver modifications retain the licenses of their upstream
files (GPL-2.0-or-later where specified). They are separate from the new
independently measured UI tables and bitmap recreations. Firmware is rebuilt
from the repository's source using the existing Retro68 toolchain; no guest
OS disk or private ROM is committed.
