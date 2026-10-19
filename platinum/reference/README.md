# Platinum reference lab

The target remains Mac OS 8.1 Platinum, with Charcoal 12 and the default
Lavender variation. Mac OS 9.2.1 is a second, separately identified raw-color
control reference. Do not treat different OS versions, color depths, font
smoothing preferences or host color profiles as interchangeable baselines.

`reference.c` draws its controls through the guest's Appearance Manager. It
contains no ClassicMac control drawing code. Build it with Retro68, boot a
**disposable copy** of a guest disk, mount the generated HFS `.dsk`, and launch
PlatinumReference. Press 1 for controls, 2 for fonts and Q to quit. Capture the
raw framebuffer with `ati/tests/guest_run.py shot`, rather than a scaled host
window screenshot. The content origin is (50,70) in the 1024×768 capture.
`kThemeStateInactive` is the disabled state; `Unavailable` is a different state.

Example build (supply your installed Retro68 toolchain file):

```sh
cmake -S platinum/reference -B output/platinum-reference \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/retroppc.toolchain.cmake
cmake --build output/platinum-reference
```

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

The system-font fixture and advances are independently rendered from the
M5Tab-Macintosh Charcoal12 BDF at commit
`bcfa1649bcbd5a6c433ad4d09b93b543ae768275`. It is a screenshot-traced pixel
recreation, not the original Apple font program. The source header records
its hash. The atlas tests all 95 printable ASCII glyphs; extended characters
and the Regular/Small fonts still use RetroBoot's original artwork.

## Running the gate

```sh
mkdir -p output/platinum-renders
PLATINUM_RENDER_DIR="$PWD/output/platinum-renders" \
  swift test --package-path app --filter PlatinumRenderTests
```

This covers individual control artwork and direct Core Text system glyphs.
It does **not** certify all whole-window layouts, text wrapping, SwiftUI Text
rasterization, menus, scrollbars, floating/modal windows or indeterminate
animation. These require additional native references and regression tests.
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
Charcoal system glyphs retain the separate 95-character pixel/advance gate.
Other fonts, menus and untested utility/modal layouts retain the earlier
coverage limits; this is not an unrestricted whole-application certificate.

The utility also exports the native standard Icon Services families into its
working directory (Startup Items when launched at startup) without resampling.
Icon Services also invokes the native Resource Manager decompressor for older
members. These are artwork acquisition aids, not guest runtime patches.
