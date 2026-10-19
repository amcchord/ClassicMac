# ClassicMac 4.1.1 — stable release complete

Stable Latest: [v4.1.1](https://github.com/amcchord/ClassicMac/releases/tag/v4.1.1),
published October 19, 2026. Release source/tag is
`dfdf4c1ad2b7206b385c1b787d72aa9af5385c2b` (PRs #28 and #29).
The release contains signed/notarized Apple Silicon Mac, Windows x64 and Linux
x64 binaries, matching prepared engine sources and SHA256SUMS (ten assets).
Fresh anonymous downloads of all nine archives match the published manifest.

Quake III Arena Demo with ATI Radeon now receives mouse motion/buttons in
menus and gameplay, and Finder input is restored after exit. Radeon startup
initializes the idle-clean state expected by Mac OpenGL, restoring the id intro
cadence. Fresh SuperDeluxe tests pass initial/replay movies, menu clicks,
aim/fire, Finder restoration and soundtrack activity. Input was simulated
through the native display protocol; physical trackpad/speaker capture was not
part of qualification.

Integrated checks pass: 125 Swift tests (two optional Copland fixture skips),
12 desktop tests, clean engine preparation/build and display/GL/device gates.
Final Windows/Linux CI and exact released-package Proxmox checks pass, including
restart, native paste and clean shutdown; Linux also passes 9p host read/write.
Mac signatures, notarization, staples, Gatekeeper and package checks pass.
See [release verification](../4.1.1-validation.md) for exact evidence and limits.

Dedicated QA VMs 170/171 are stopped and retained. Windows QA auto-login,
stored password and scheduled task are removed; the read-only source mount is
detached. User gxmetal work, machines and unrelated worktrees are preserved.
No Library catalog, DNS or Apache changes were made.

No release work remains. Retain stable v4.1.0 and its assets for rollback:
`gh release edit v4.1.0 --latest`, then record the reason in 4.1.1 notes.
Task worktree and ignored qualification output remain under
`worktrees/q3-input-cinematic` for support and reproducibility.
