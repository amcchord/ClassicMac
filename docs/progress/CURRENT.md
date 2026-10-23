# ClassicMac 4.5.0 — stable release complete

[Stable Latest v4.5.0](https://github.com/amcchord/ClassicMac/releases/tag/v4.5.0),
published October 23, 2026. Exact source/tag:
`69f3d52ed721599542afe231bd5820549ba07813` (PR #35).
All ten assets are public; all nine fresh anonymous downloads match SHA256SUMS.
The public Mac DMG passes full exact verification, staples and Gatekeeper.

Windows now ships as one portable EXE and Linux as one AppImage, with an
optional DEB. Shared Mac measurements, bitmap fonts, document/title controls
and the home layout replace the approximate theme. All operations are in the
window, without a host menu bar. Electron remains the internal desktop host.
Measured components and actual captured controls/composed title frames pass
at 1×/2× with zero differing pixels. Windows/Linux home captures are identical.
Windows WindowShade uses an exact native 23-pixel drawing/input region.

Native Windows CI 40037702912 and Linux CI 40037705241 pass the complete fresh
matrix at runtime/UI source 956dd7d. Both Proxmox hosts independently pass both
setup routes, public ATI download/import/first Finder boot/normal shutdown,
and exact final portable UI/boot/restart/shutdown checks. All 61 packaged
source/UI files match. Root checks pass 131 Swift tests (two optional skips),
all 22 desktop tests, deterministic exports and bundle syntax.

Mac retains qualified 4.4 native frontend/engines with new version metadata,
original entitlements and accepted 4.5 notarization. Prepared source archives
remain byte-identical to qualified 4.4, with NDRV verified.

No release work remains. Dedicated QA VMs 170/171 are stopped; task scheduled
tasks/RDP are closed and temporary notary key files are removed. Windows QA
storage grew from 100 to 228 GiB without deleting data; Linux QA got FUSE 3.
Task artifacts are retained. User machines, root gxmetal and unrelated
worktrees are preserved. No catalog, DNS, hosting or production service change.
Windows remains unsigned; Copland remains Mac-only. Physical GPU/audio and
untested whole-window/scrollbar/menu states retain documented coverage limits.

Rollback: `gh release edit v4.4.0 --latest`; record the reason. Existing
machines and the public Library catalog are unchanged.
