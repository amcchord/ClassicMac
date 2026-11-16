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
Fresh Mac build and CGL/cursor gates pass. Initial Linux CI 43787660818 and
Windows CI 43787664760 pass from 65b0c53, including 598,030 exact cursor pixels.
Actual Mac defaults exposed the existing IRQ conflict: controlled old-engine
boots with native device order stall at automatic slot 18 for 65 seconds;
slot 19 alone reaches Finder in 24.44 seconds and cleanly shuts down. Swift
and the native oracle now share the already qualified Windows/Linux fix.
New argument test passes. Ubuntu is online after boot recovery; 150 UI steps
pass. The 43-step edge flow caught QA rounding of the last pixel center;
its bounds check now uses the actual floating-point viewport. Fresh final
packages must include those last launcher/qualification changes. Preserve root gxmetal and other worktrees.

Next: finish engine build, run cursor and existing GL/display gates, build
all three packages, test actual guest edges and release regression flows,
consolidate validation, commit/push and publish verified 4.6.4 downloads.
Use only owned QA VMs on requested Proxmox; no catalog/site deployment.
