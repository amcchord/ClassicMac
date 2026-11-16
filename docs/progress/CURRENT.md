# RetroBoot Radeon cursor import — 2026-11-15

User requests the GPU fix just added to RetroBoot. Confirmed source is
88d074a55b00af7507e9412734ee83fd988876b3, Radeon Mac OS 9 cursor clipping,
prepared for RetroBoot 2.0.2 at 1c9a702. Scope: cursor composition hunks and
state from retroboot-macosx-agp.patch, excluding OS X/AGP platform changes.

Owner: codex/retroboot-gpu-cursor in worktrees/retroboot-gpu-cursor.
Baseline main 473a525; published v4.6.3 remains the qualified rollback.
New portable qtest reproduces the bug on the old qualified engine at the
first clipped-left pixel (black instead of white). The new patch passes
application checks against the qualified prepared QEMU source.
Fresh independent Mac engine build is active; CI gates and 4.6.4 package
versions are being prepared. Preserve root gxmetal and other worktrees.

Next: finish engine build, run cursor and existing GL/display gates, build
all three packages, test actual guest edges and release regression flows,
consolidate validation, commit/push and publish verified 4.6.4 downloads.
Use only owned QA VMs on requested Proxmox; no catalog/site deployment.
