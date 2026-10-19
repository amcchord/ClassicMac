# ClassicMac 4.1.1 — release checks in progress

Worktree: worktrees/q3-input-cinematic, branch codex/q3-input-cinematic.
PR #28; source 892b1d0 (test/doc follow-up may advance). Root main remains
11a4c61; unrelated root gxmetal work is preserved. User authorized a stable
release and clarified the Quake III report is on macOS.

Both fixes pass fresh private SuperDeluxe v2 tests. USB HID and position-aware
ATI cursor routing provide menu motion/clicks, aim/fire and seamless Finder
return. Radeon WAIT_UNTIL's idle-clean mask was the intro cause: controlled
0 <-> 0x30000 changes reproduce ~3 <-> 30 uploads/s without restarting the
app or sound. Reset/handoff initialization now fixes first launch and replay.

Independent clean engine build/display/GL gates and Screamer synthetic DMA
checks pass. Signed Mac package: menu, aim/fire, first intro 203 uploads in
8 s, replay 183 in 6 s, Finder return PASS (mac-release-qualified). Separate
WAV test passes, with 481 active 10 ms soundtrack bins (sound-cadence-final).
Mac packaging is complete; notarization is running. Windows CI 39245242603
(retry after copied-source CRLF fix) and Linux CI 39244841150 are building.
No release or tag has been published.

Dedicated Proxmox QA VMs 170/171 are running. Windows 171 temporarily has QA
auto-login enabled for interactive package tests: disable AutoAdminLogon,
remove DefaultPassword and unregister the task after testing. Never log or
copy the protected test password from the root output/4.1.0/private file.
The user VM PID 75833 must remain untouched. Old diagnostic test guests were
stopped; only current qualification clones are task-owned. Source SuperDeluxe
mount /dev/disk10 is read-only and should be ejected at completion.

Next: finish native CI packages and Proxmox smoke, notarization and freshness;
merge PR, run consolidated root tests once, publish stable Latest v4.1.1 with
all platform binaries, prepared engine sources and checksums; verify public
assets and update release evidence. Rollback is retained stable v4.1.0.
