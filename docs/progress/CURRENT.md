# RetroBoot import and desktop UI audit

Owner: `codex/retroboot-ui-audit`, contained worktree
`worktrees/retroboot-ui-audit`. Baseline `5835124`; engine `0d7cd63` and UI
`a5dce20`, with audit/evidence, are integrated into local `main`. No push, tag, release or production change was requested or performed.
The previous 4.6.2 release candidate remains unpublished; this task does not
complete or resume its release workflow.

RetroBoot 2.0.1 (`432a43d4`) is imported: 16 engine patches (15 verbatim,
one shared-VIA adaptation), relative 16 ms Power Mac VBL and Quadra GetPage
fix; both firmware files rebuilt. Two source preparations and both QEMU
v11.0.2 builds pass, with ClassicMac CGL/WGL/resize/input policies retained.

The Windows/Linux C++ UI now uses native measured Mac Roman fonts, menus,
movable dialogs, focus, paired-arrow scrollbars and progress phases. Sharp
Pixels scrolls oversized screens, full screen is content-only, narrow action
bars wrap, Media is available and custom resolutions survive configuration.
Swift shared controls/fonts receive the same reference corrections.

Validation: native 122 zero-difference reference comparisons plus 672 glyphs
and advances; Swift 138 tests, three optional fixture skips, no failures;
11 Python tests; real CGL draw, driver and Screamer device checks pass.
Two 48-step Power Mac flows pass at 1x/modeled 2x; built-in and enhanced
Quadra 16-step restart/resize flows pass. Eleven guest captures compare
6,843,569 visible pixels exactly. Details and sanitized evidence are in
`docs/ui-fidelity-audit-2026-11.md` and `docs/evidence/2026-11-ui-audit/`.

Limits: actual Windows build/compositor/DPI validation was unavailable
(existing VM guest agent is not running); no VM settings/state changed.
Mac SwiftUI whole-layout text and small-window smooth guest fitting still
need a separate physical-pixel pass. ATI game drawing/input works, but both
old/new renderer fixtures produce silent audio; audio is not qualified.

Next safe action: build the final Windows package and run actual
100/125/150/175/200% DPI desktop/keyboard/lifecycle checks when a working
Windows execution channel is available. No publication is authorized.
Root untracked `gxmetal/`, original guest disks and unrelated worktrees remain
untouched. Task-owned reference/QA guests are stopped; local evidence and
prepared engines remain inside the project for review.
