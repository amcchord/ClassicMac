# Platinum appearance

ClassicMac draws its own interface in the Mac OS 8/9 "Platinum" appearance,
shared with RetroBoot so the two products look and behave alike. The font programs, original drawn icons and measured control drawing code are MIT
licensed. Original Apple Mac OS 9 artwork in `macos-icons/` has separate
provenance and copyright; it is not MIT licensed. Reference screenshot crops are test evidence, kept separate from
shipping app resources; their provenance is recorded in `reference/README.md`.

- `fonts/` — RetroBoot System 12, Regular 12 and Small 10 bitmap fonts:
  original pixel art "in the spirit of" the classic system fonts (see
  `LICENSE-fonts.txt`). Source: RetroBoot `presenter/ui/assets/fonts`
  at `531f953` (2026-10-11). The system font’s printable ASCII glyphs now
  use the Charcoal 12 pixel recreation measured in M5Tab-Macintosh; the BDF
  records its source hash. Extended glyphs retain the original artwork.
- `icons/` — the matching icon set and palette (see `LICENSE-icons.txt`),
  from RetroBoot `presenter/ui/assets/icons` at the same revision.
- `tools/build_platinum_swift.py` — compiles the bitmap fonts to pixel-square
  TrueType (64 units per pixel; Regular and Small also get QuickDraw-style
  synthetic bold) and embeds fonts and icons in
  `app/Sources/PlatinumKit/Generated/PlatinumAssets.swift`. Standard library
  only. Run it after changing anything here.

The initial control tables came through RetroBoot rbui from M5Tab-Macintosh.
The 2026-10-18 fidelity audit corrected raw framebuffer gray levels, state
artwork, pop-up arrows, title boxes, default rings and determinate progress.
`MeasuredChrome.swift` ports RetroBoot rbplatinum/chrome.py at `237fe033`
(MIT), with fresh native captures for the formerly approximate disabled
radios, mixed checkbox, pressed pop-up and disabled default ring. See
[the reference lab](reference/README.md) for exact coverage and limitations.

One Mac pixel is one point. PixelPainter controls and direct system text are
tested at both 1× and 2× device scales. SwiftUI Text and whole application
layouts require separate verification; the control gate is not a blanket
pixel-perfect certification.

- `macos-icons/` — original Mac OS 9 Icon Services families and selected native
  resources, with source/member hashes. The exporter preserves full native
  colour/alpha and separate 16/32-pixel strikes for Mac and desktop frontends.
  `tools/extract_macos_icons.py --check` verifies deterministic exports.
