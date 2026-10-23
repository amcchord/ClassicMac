# ClassicMac 4.5 — publication preflight

PR #35, contained `worktrees/4.5-desktop-platinum`, branch
`codex/4.5-desktop-platinum`, runtime/UI source `956dd7d`. All requested desktop
work is implemented: portable Windows EXE/Linux AppImage, measured Mac Platinum
controls/bitmap fonts/window frame, classic title widgets/WindowShade and no
host menu bar. Both setup and machine operations remain in the window.

Both native Proxmox hosts pass complete guest/setup checks. Exact final files
repeat UI/pixels at 1×/2×, composed title frame, actual HWND region, Finder,
pause/restart/reconnect and normal shutdown. All 61 source/UI files match each
package. Windows/Linux home captures are pixel-identical. Linux native CI
40037705241 and Windows 40037702912 pass the complete fresh setup matrix.

Mac 4.5 metadata-only app/DMG is signed, notarized, stapled and fully verified,
with original entitlements and unchanged native frontend/engine code. Matching
prepared engine source archives are staged. Local Swift 131/desktop 22 checks
pass; deterministic exports pass. Ten release assets, including SHA256SUMS,
are staged locally. No public 4.5 release/tag yet.

Next: integrate PR, run consolidated root checks once, publish stable Latest,
then verify every fresh anonymous download and stop dedicated QA hosts.
QA VM 171 grew from 100 to 228 GiB with data preserved; Linux QA got FUSE 3.
No user machine, catalog, DNS, hosting configuration or production service was
changed. Root gxmetal/other worktrees remain. Rollback: promote v4.4.0 Latest.
