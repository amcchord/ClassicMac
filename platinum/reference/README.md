# Platinum reference lab

The target remains Mac OS 8.1 Platinum, with Charcoal 12 and the default
Lavender variation. Mac OS 9.2.1 is a second, separately identified raw-color
control reference. Do not treat different OS versions, color depths, font
smoothing preferences or host color profiles as interchangeable baselines.

`reference.c` draws its controls through the guest's Appearance Manager. It
contains no ClassicMac control drawing code. Build it with Retro68, boot a
**disposable copy** of a guest disk, mount the generated HFS `.dsk`, and launch
PlatinumReference. Press 1 for controls, 2 for the original font sample,
3/4 for document frames/title widgets, 5/7 for active/inactive movable
dialogs and menus, 6 for scrollbars/focused controls, 8/9 for indeterminate
phases, A/B/C for native Charcoal 12/Geneva 12/Geneva 9 atlases, and Q to quit. Capture the
raw framebuffer with `ati/tests/guest_run.py shot`, rather than a scaled host
window screenshot. The content origin is (50,70) in the 1024×768 capture.
`kThemeStateInactive` is the disabled state; `Unavailable` is a different state.

Example build (supply your installed Retro68 toolchain file):

```sh
cmake -S platinum/reference -B output/platinum-reference \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/retroppc.toolchain.cmake
cmake --build output/platinum-reference
```

The utility requests 32-bit video and displays the actual Gestalt system
version and depth. The November captures report `Mac OS 921, depth 32`.
If Retro68 produces a blank `.dsk`, wrap its `.bin` application without
changing global hfsutils mount state:

```sh
python3 -m venv output/reference-venv
output/reference-venv/bin/pip install machfs Pillow
output/reference-venv/bin/python platinum/reference/make-disk.py \
  output/platinum-reference/PlatinumReference.bin output/reference.dsk
```

`make-disk.py` refuses to replace an existing image. Mount that image as a
read-only auxiliary disk in a disposable guest. Capture every page from the
raw framebuffer with the pointer outside the measured rectangles.

## Committed regression fixtures

`app/Tests/ClassicMacTests/Fixtures/Platinum` contains small reference crops,
not captures from the production renderer. Their origin and rectangles are
recorded in JSON alongside them. `PlatinumRenderTests` compares every device
pixel at both 1× and nearest-neighbor 2×; there is no percentage tolerance.
Normal, pressed and disabled buttons, default rings, checkboxes (including
mixed), radios, pop-ups, group boxes and determinate progress are covered.

The three title-box references come from Nathan Lineback's original Mac OS
8.1 screenshots at [ToastyTech](http://toastytech.com/guis/macos81.html).
Raw crops are retained. These screenshots have quantized colors (for example,
a gray becomes RGB 140,140,132), so the **pattern** fixtures explicitly map to
the nearest 17-step grayscale level. This normalization is only used for
those three OS8 title-box patterns. OS9 control crops compare raw RGB values.
The fetch manifest lists ten lossless source screenshots and their SHA-256s.
Full archive images remain local test evidence and are not app resources.

The current font fixtures come from the guest's own unsmoothed QuickDraw
DrawChar/CharWidth calls. `native-fonts.json` records the raw-frame hashes,
utility hash, atlas rectangle, pen position and the 224-character Mac Roman
mapping. Each cell encodes its native advance in eight binary pixels, so the
same independent crop verifies shape, bearing and advance. The three fonts
cover 672 glyphs. The existing 95-character system ASCII gate also uses this
native capture. Its previous M5Tab-Macintosh-derived oracle is retained as
`m5-system-ascii-legacy.png`; it had correct advances but a one-pixel bearing
error. This is an intentional reference correction, not a relaxed comparison.

`platinum/tools/measure_bitmap_fonts.py <raw-font-frame-directory>
--utility <PlatinumReference.bin>` measures the three screenshots named
`charcoal12.png`, `geneva12.png` and `geneva9.png`, updates the BDF recreations
and writes the independent crops/provenance. It rejects smoothed glyphs and
invalid advance encodings. No Apple font program is read or distributed.
Run `build_platinum_swift.py`, then `native/scripts/export-assets.py` to rebuild
shipping resources. TrueType timestamps use UTC for deterministic exports.

## Running the gate

```sh
mkdir -p output/platinum-renders
PLATINUM_RENDER_DIR="$PWD/output/platinum-renders" \
  swift test --package-path app --filter PlatinumRenderTests
```

This covers individual control artwork and direct Core Text glyphs. The
November additions cover menu panels, active/inactive movable dialogs,
focus frames, paired-arrow scrollbars and all 16 indeterminate phase values
(four repeating patterns). `os9-auxiliary-reference.json` records the native
source hashes and crop rectangles. These are fresh measurements from the
Appearance Manager, independent of ClassicMac and RetroBoot renderers.
It does **not** certify all whole-window layouts, wrapping, SwiftUI Text
rasterization, unmeasured utility/modal variants or animation timing.
Do not describe the entire application as pixel-perfect based on this gate.

## Mac OS 9 document window gate (4.2)

Pages 3 and 4 capture native Appearance Manager document frames and held title
widgets. Page 3 uses titleHeight 16/titleWidth 70, a 320×140 content rectangle
and active/inactive/collapsed states. It also draws 100×22 small bevel buttons
in normal/pressed/disabled states. Page 4 uses DrawThemeTitleBarWidget to draw
the held widgets. Its widget-active state is the held state; it must not be
confused with the enclosing window's active state. Source framebuffer hashes
and exact rectangles are in os9-window-reference.json beside the fixtures.

The gate covers the entire document border, stripe gaps, content offsets,
shadow, inactive palette, grow-box notch, all three held title boxes and VM
toolbar button edges at 1× and 2× with zero differing pixels. A composed SwiftUI
view test paints opaque content to ensure it cannot hide the native grow box.
The November font and auxiliary-control gates extend this coverage as
described above. Untested window variants and whole application layouts
still prevent an unrestricted whole-application certificate.

The utility also exports the native standard Icon Services families into its
working directory (Startup Items when launched at startup) without resampling.
Icon Services also invokes the native Resource Manager decompressor for older
members. These are artwork acquisition aids, not guest runtime patches.

## Native Windows/Linux renderer (4.6)

`native/scripts/export-assets.py` exports the Mac measurement tables and BDF
fonts deterministically, including the editable-field TrueType resources.
The November compiled C++ renderer compares 61 independent captures at
1×/2× (122 zero-difference comparisons), 672 native glyphs and advances,
actual focused fields/buttons/toggles/menu delegates, opaque-content window
compositing, and the complete home's exact integer expansion at 2×.
Item delegates and scrollbars explicitly disable smoothing.

See the [4.6 qualification record](../../docs/4.6.0-validation.md) for actual
historical host coverage. The [November audit](../../docs/ui-fidelity-audit-2026-11.md)
distinguishes this task's modeled DPI tests from actual Windows testing.
The [4.5 record](../../docs/4.5.0-validation.md)
retains the qualification of the retired Electron renderer. Independent guest
fixtures remain reference material; they are never substituted for shipping UI.
