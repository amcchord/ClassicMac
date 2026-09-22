# Copland Activities — publishing 3.2.1

Follow-up work is in `worktrees/copland`, branch `codex/copland-apps` from GitHub
main `d0d0ef3`; the root iPad checkout is preserved. Runtime, guide and disk work
is complete. GitHub PR/release and final catalog promotion are next.

Implemented persistent guest RTC with migration from real HFS dates, release of
the startup Caps Lock gesture on actual keyboard input, an activity guide in the
app, and a repeatable 512 MiB image builder. A hash-pinned guest clipboard patch
fixes two zero-byte operations without suppressing other assertions. Anarcho
editing/saving/reopening, MineSweeper, Solitaire card movement and GXSlidemaster
provide activities. Classic app quit paths remain unstable.

Final disk: `output/copland/apps-research/activities-v3-final.img`, SHA-256
`1028802601ccabc4fad5a2e31eac783b811d6092022de5f1ac17d76d219f8036`.
It includes original guides and links to Apple documentation; local DDK/manual
research images are not published. The builder verified 551 original/generated
file forks and both added app forks. v3 corrects two game-guide sentences from
the unlisted v2 candidate. Both immutable archives exist on mcchord.net, but the
catalog still selects v1 until 3.2.1 is available.

110 Swift tests pass, plus opt-in production import and signed-helper lifecycle
checks. Text survives restart, lowercase input works, and the native restart
requires zero debugger continuations. The final app and DMG are notarized and
stapled; exact mounted-DMG verification passed. Exported runtime sources rebuilt;
the final source archive differs only in two guide texts. Evidence and artifacts
are under `output/3.2.1`; see `docs/3.2.1-validation.md` for limits and hashes.

The host is locked; real guest framebuffer/input and app-manager tests were used.
Physical macOS 15 execution was not tested. macOS 27's read-only fsck_hfs attempt
terminated with SIGTRAP, so no fsck pass is claimed. No user machines were changed.
