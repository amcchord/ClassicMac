# Platinum fidelity audit — October 18, 2026

The application was **not pixel-perfect** when this audit began. Several
controls were deliberately approximate, and the original font notice correctly
called the fonts artwork “in the spirit of” the originals. Rendering a gallery
successfully was not evidence of fidelity.

The target remains the documented Mac OS 8.1 appearance. Ten original OS 8.1
screenshots were collected from Nathan Lineback's ToastyTech archive; source
URLs and SHA-256s are in `platinum/reference/archive-sources.json`. Apple's
[Mac OS 8 Human Interface Guidelines](https://dev.os9.ca/techpubs/mac/pdf/HIGOS8Guidelines.pdf)
provide layout context. Raw Mac OS 9.2.1 frames from a disposable local guest
provide a separately labeled control-state/color baseline. The guest reference
utility calls Appearance Manager directly, without ClassicMac drawing code.

## Findings and fixes

| Area | Before | Verified now |
| --- | --- | --- |
| System font ASCII | 17/95 glyph shapes and 45/95 advances matched the M5Tab reference | 95/95 shapes and advances, direct Core Text at 1× and 2× |
| Representative text widths | Cancel 38, ClassicMac 64, Shut Down 60, Name: 34 | 42, 70, 66, 39 pixels |
| Grays | Host-adjusted values such as face E6 and frame DE | Raw framebuffer DD and CC; raw-color control comparisons |
| Push/default buttons | Approximate pressed/disabled color substitutions | All three states, zero differing pixels |
| Checkboxes/radios | Approximate state substitutions; mixed mark approximation | Off/on/mixed and pressed/disabled captured states, zero differing pixels |
| Pop-up | Single down arrow and push-button geometry | Native paired arrows, separate arrow cap and all three states |
| Progress/group/focus/sunken frames | Approximate gradients/edges, two wrong etch corner pixels | Native determinate progress at 30/70%, group, focus and sunken frame |
| Title boxes | Reversed diagonal gradient, different outlines/widgets | Close/zoom/collapse patterns match OS8 screenshot crops |
| Titles | Wrong font widths and softened SwiftUI drawing | System strike drawn directly with truncation; OS8 box/stripe offsets corrected |

There are 32 control-state/pattern fixtures, compared at both 1× and 2× with
**zero pixel tolerance**. Three OS8 title-box fixtures normalize the archive's
quantized colors explicitly; raw crops remain beside them. The other 29
compare raw RGB. The system atlas adds 95 glyphs and advance checks.

## Remaining gaps

This is a component audit, not a blanket pixel-perfect certificate. SwiftUI
Text elsewhere can still smooth outline-based bitmap fonts. Regular/Small and
extended system glyphs remain original approximations. Menus, scrollbars,
grow boxes, inactive/floating/modal/full-screen window states, text baselines
and wrapping throughout application layouts, and indeterminate animation need
additional native screenshot coverage. Pressed title-box feedback also needs
its own native fixture. Mac OS 9 font smoothing is a separate appearance
setting; it must not silently replace the OS8 mono-font target.

The current gate detects regressions in the corrected components. Extending
it means collecting a native reference with its OS, depth, palette and state,
recording the crop, and comparing it before changing the production renderer.
See [the reference lab](../platinum/reference/README.md) for reproduction.

## October 19 — Mac OS 9 icons and running-VM frame

The 4.2 pass uses native Mac OS 9 Icon Services families, including original
32-bit colour/8-bit alpha and separate 16/32-pixel artwork. Source/family/member
hashes and the native acquisition method are in `platinum/macos-icons/`.

Independent Appearance Manager captures add full document-frame comparisons:
active, inactive and WindowShade-collapsed, small bevel toolbar buttons in all
three states, held close/zoom/collapse boxes and the native diagonal resize
corner. All newly covered states compare with zero pixel tolerance at 1×/2×.
This fixes the one-pixel top/content offset, title stripe gaps and box offsets,
shadow endpoints, inactive gray levels, missing held feedback and a grow box
which opaque guest content previously covered. Toolbar labels use direct
Charcoal-strike drawing; narrow VM windows retain controls through a compact
icon layout. Focus comes from the owning NSWindow's key state.

This extends document-window and icon coverage. The remaining font/layout,
menu, utility/modal and animation gaps described above still need their own
native references. Desktop CSS shares the original icon PNGs and corrected
title stripe pattern, but is not certified by the native Swift frame gate.
