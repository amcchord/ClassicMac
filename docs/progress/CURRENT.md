# ClassicMac 4.6 — native desktop release in progress

Owned worktree `worktrees/native-desktop`, branch `codex/native-desktop`, based
on PR #36 at 52b775e. Root main remains the integration checkout; gxmetal and
unrelated worktrees are preserved. Stable Latest is still v4.5.0.

User explicitly requested removing Electron, compiled Windows/Linux frontends,
Mac Platinum pixels, single-executable packages, Proxmox testing, PR #36 base
images, and publication. C++/Qt native widgets, integer bitmap renderer, direct
RFB/QMP, machine management, setup, download/import and hardware profiles are
implemented. No web view is used by the new frontend. Native local tests pass
84 independent pixel captures at 1x/2x, 95 glyph advances, actual native button
compositing, startup argument parity, sparse disks, paste and VNC auth.

Next: native CI builds and Proxmox qualification of exact portable packages,
then integration, full Mac refresh/signing/notarization and release publication.
QA VMs 170/171 are running. Qt build dependencies were installed on VM170.
No release or production catalog changes in this task. Keep v4.5 for rollback.
