# ClassicMac 4.2.0 — stable release complete

[Stable Latest v4.2.0](https://github.com/amcchord/ClassicMac/releases/tag/v4.2.0),
published October 19, 2026. Exact release source/tag:
`9ca105f583ba9e220f4795790032901da56f5c14` (PR #31). All ten assets are public;
fresh anonymous downloads of all nine archives match SHA256SUMS.

Original Mac OS 9 Icon Services artwork replaces system-icon substitutes,
including native 16/32-pixel strikes and 8-bit alpha. Mac document frames and
controls cover 42 independent pixel references at 1×/2×; all 95 system glyphs
and advances pass. Real signed-app checks cover SuperDeluxe boot/display,
compact toolbar, WindowShade/restore and Media icons. Source/family hashes
and Apple attribution remain separate from MIT and Iconfactory assets.

Consolidated root checks: 129 Swift tests, two optional Copland fixture skips,
zero failures; 12 desktop tests pass. Final native Windows 39267099712 and
Linux 39267103833 pass packaged boot/restart/shutdown and actual toolbar
geometry. All 54 packaged source/assets match on each platform. Mac app/DMG
signatures, notarization/staples, Gatekeeper and full release verification
pass; fresh public DMG checks also pass. See docs/4.2.0-validation.md for scope.

No release action remains. Task guests stopped normally; mounts detached;
temporary notary key removed. User gxmetal work, machines and unrelated
worktrees are preserved. No catalog, DNS, Apache or Proxmox changes in this
UI release. Worktree/output remain in worktrees/native-platinum-icons.

Rollback: `gh release edit v4.1.1 --latest`, then record the reason in 4.2 notes.
Whole-app fonts/layouts and desktop CSS retain the documented fidelity limits.
