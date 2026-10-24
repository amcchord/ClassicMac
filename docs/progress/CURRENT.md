# ClassicMac 4.6 — qualified, awaiting publication

PR #36 and PR #37 are merged on main (1a5cae8). Native C++/Qt replaces
Electron on Windows/Linux, with measured Platinum rendering, preserved
machines/preferences and single EXE/AppImage downloads. Library supports
System 7.5.5, Mac OS 8.1 and Mac OS 8.6 and matching engines.

Both final files pass Proxmox setup, public image import, native input, UI
at 1x/2x, minimum size, WindowShade and three-base lifecycle checks. Actual
home captures have zero scaling differences; active homes match between
platforms. All package inventories pass. Mac app/DMG are signed, notarized,
stapled and release-verified. Consolidated root Swift/native/packaging checks
pass; details and exact hashes are in docs/4.6.0-validation.md and evidence.

Next: tag and publish v4.6, verify all anonymous downloads and exact public
Mac DMG, then stop task QA services and both Proxmox VMs. Latest is v4.5
until publication. Preserve root gxmetal and unrelated worktrees.
