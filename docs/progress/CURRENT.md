# ClassicMac 4.6 — final native release qualification

Owned worktree `worktrees/native-desktop`, branch `codex/native-desktop`.
Native C++/Qt replaces Electron on Windows/Linux, preserving measured Platinum
pixels, existing machines and single EXE/AppImage downloads. PR #36 provides
System 7.5.5, Mac OS 8.1 and Mac OS 8.6 and the matching engine changes.

Both Proxmox hosts pass the three public base imports, native input and
lifecycle, both setup routes, 1x/2x UI, minimum size and WindowShade. Final
renderer source ff11171 fixes list interpolation and the Qt list border;
final package CI/qualification precedes integration and publication. Mac
4.6 app/DMG are signed, notarized, stapled and release-verified.

Next: verify final portable files and inventories, merge PR #37, consolidate
root validation, publish v4.6 and verify anonymous downloads. Latest remains
v4.5 until publication. QA VMs170/171 and own RDP session are running.
Preserve root gxmetal and unrelated worktrees; stop QA hosts after release.
