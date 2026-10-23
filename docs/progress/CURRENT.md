# ClassicMac 4.5 — portable Platinum desktop

Active branch: `codex/4.5-desktop-platinum` in contained
`worktrees/4.5-desktop-platinum`, baseline main `14cbebd`.
Austin requested single executable Windows/Linux downloads, the Mac app’s
measured Mac OS 9 UI without a host menu bar, real-host testing and publication.

The shared pixel renderer matches the independent native Mac fixtures and
Charcoal bitmap glyphs at 1×/2×. UI integration and single-file packaging are
in progress. Dedicated Proxmox QA VMs 170/171 are started; no existing user
machine is used. Root untracked gxmetal and other worktrees are preserved.

Next: finish live UI checks, build portable EXE/AppImage, qualify those exact
files on native Windows/Linux, then integrate and publish stable v4.5.0.
Rollback: retain stable v4.4.0; no catalog, image, DNS or hosting changes planned.
