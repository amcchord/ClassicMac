# Actual ATI and Quadra resizing — release qualification

Owner: `codex/ati-quadra-resize`, contained worktree `worktrees/ati-quadra-resize`.
User requests actual guest mode changes for ATI and Quadra, Proxmox testing,
and a new stable release. v4.6.1 / root86e7fc0 remains published Latest/rollback.

Quadra root cause: gray-screen fill subtracts8 from an unsigned byte count.
A four-byte remainder wraps the counter and overwrites guest memory. The bounded
word fill passes19,974 width/depth canary cases. The original driver plus the
fill fix passes early resize, grow/shrink, healthy Finder and shutdown on VM170.
Experimental callback/ROM/ID changes were removed. Pending requests now survive
Display Manager startup and rejected attempts.

ATI uses a dedicated PCI mailbox and a firmware NDRV service; nothing is installed
in guest disks. Two inactive programmable timing slots alternate through ATI's
own video driver and Display Manager. Six exact grow/shrink modes plus clean
shutdown/reboot pass on VM170 (37 steps). Production cleanup adds bounded retries,
verified completion, safe filter ownership and a 500ms debounce. Qualify this final
source on Windows/Linux including limits, startup, pause, DPI, full screen and3D.

Mac engines build and pass gates; focused Swift34 tests, native pixels/RFB/input/
models/export, and bounded-fill test pass. Fresh shared firmware is rebuilt.
Version4.6.2 is being prepared; no tag/release/PR yet. Start Windows/Linux CI.

QA VMs170/171 are running (originally stopped). VM170 has isolated qa462 sources,
runtime and sparse fixture copies and owned Xvfb99. Original fixtures/configs and
user machines untouched. End own resources and restore both VMs to stopped after
publication. Root gxmetal and unrelated worktrees preserved.
