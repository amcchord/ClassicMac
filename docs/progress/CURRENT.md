# Copland Activities — 3.2.1 published

ClassicMac [v3.2.1](https://github.com/amcchord/ClassicMac/releases/tag/v3.2.1)
is public with notarized/stapled DMG and ZIP, corresponding engine source and
checksums. [PR #21](https://github.com/amcchord/ClassicMac/pull/21) merged as
`cf39faf566b0325e036acaed45ff365a9f9dfd25`; runtime source is `f3627cd`.
The root `codex/ipad-parity` checkout remains untouched.

The mcchord.net catalog selects **Copland D11E4 Activities**, a 512 MiB disk with
Anarcho text editing, MineSweeper, Solitaire, GXSlidemaster, original experiment
guides and a notebook. Existing machines and the OS 9 entry are preserved.
Persistent RTC fixes repeat-boot directory-date assertions; startup Caps Lock
releases on real input; a narrowly pinned clipboard patch enables text saves.
Classic app Quit and some Control Panels remain unstable.

110 Swift tests pass, plus actual production import and signed-helper lifecycle
checks. A saved mixed-case document survived restart; card movement, mine reveal/
flag/new game, and live graphics slides work. The final public archive booted and
restarted with zero debugger continuations. Corresponding runtime sources rebuilt.
A fresh GitHub DMG download passed the full mounted-release/signature checks.
See `docs/3.2.1-validation.md` and `docs/operations/classicmac-downloads.md` for
hashes, backups, limitations and rollback. Evidence is under `output/3.2.1`.

The host was locked, so real guest framebuffer/input and app-manager tests were
used. Physical macOS 15 execution was not tested. macOS 27's read-only fsck_hfs
attempt terminated with SIGTRAP; no fsck pass is claimed. All task emulators are
stopped, no user machines were changed, and no release action remains.
