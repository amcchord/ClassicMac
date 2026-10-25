# Actual guest resizing and whole-pixel UI — final release qualification

Owner:codex/ati-quadra-resize, contained worktree worktrees/ati-quadra-resize.
User explicitly requests actual ATI/Quadra modes, robust Windows pixel-art UI,
Proxmox testing and publication. User chose whole-pixel UI zoom at fractional
Windows DPI. v4.6.1 remains Latest/rollback; no4.6.2 tag/release yet.

Final compiled candidate:c5184eadda364e5b95428709095bb811f3cc965e. ATI service
b41c9f3 changes real modes during Quake III; QFB9eb2bcb bounds gray fills and
passes19,974 canary rows plus six real guest depths. Whole UI uses1×/2×,
Per-Monitor V2 reports real OS DPI; full-UI checker caught/fixed stock grip.
Native drag preserves2× client grid. Resizes now fit the work area. VGA explicit
retries re-arm late requests; five-minute budget covers older booting guests.

Focused native tests and Mac engine gates pass. Final Mac signing/notary/verifier
pass (DMG8a336e5). Final Linux CI40146327957 passes; Windows40146326685 builds.
Owned Linux prototype passes25 early8.6 resize/reboot steps with the retry fix.
Final package Linux lifecycle/UI/game tests and Windows confirmation are active.
PR39 is draft/attached. Finish package checks, integrate, run consolidated root
preflight once, publish/verify all nine assets, then restore QA state.

VM170/171 originally stopped, currently running. Own Xvfb99 and100;100 uses
-noreset for Xft DPI tests. Own Windows tasks462QA/Capture/Drag and RDP/SSH
forward must end. All guest tests use isolated clones; originals untouched.
Root untracked gxmetal and unrelated worktrees remain intact.
