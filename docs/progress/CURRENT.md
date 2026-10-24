# Classic OS base images — uploaded; client integration pending

Owned branch `codex/classic-base-images`, worktree `worktrees/classic-base-images`.
ClassicMac source checkpoint 36432ca; matching RetroBoot branch b475e3d builds on
D044 hostd e473f4c. Root/main and unrelated work are preserved.

All requested immutable v1 archives are public at https://mcchord.net/classicmac/:
- system-7.5.5-base-v1.tar.gz — 20,511,664 bytes, SHA256 68b8ffb1...38246.
- mac-os-8.1-base-v1.tar.gz — 36,423,160 bytes, SHA256 779cb74e...a3d09.
- mac-os-8.6-base-v1.tar.gz — 56,692,342 bytes, SHA256 f7e61bc9...f9a88.
Each has StuffIt Expander 5.5, Transmit 1.2 and Netscape (3.04 on 68k, 4.5 on 8.6).
Source disks are scrubbed, free HFS allocation blocks zeroed and frozen read-only.
Private sources, complete hashes and evidence live in root output/classic-base-images.
No proprietary guest content is in Git.

Catalog-v2.json now preserves the six legacy rows and lists all three bases (9).
SHA256 942b2bb05f44302f2a36a24d9fa45e72876497e5a67f971cc80177b6c2b7c5bc.
Legacy catalog.json is unchanged (21661864...cf85). Latest rollback on the server:
/root/classicmac-catalog-backups/pre-base86-v2-20261024.json. No server restart.
User explicitly authorized archive upload and catalog setup.

Verification: full public HTTPS downloads, exact hashes, Range 206 and production
Swift/desktop/RetroBoot archive imports pass. Fresh imported disks boot and accept
Finder input. 68k Mac/Linux boot/input and native HTTP, PASV FTP/StuffIt pass.
8.6 passes repeated 4 MiB FTP/StuffIt transfers on three fresh Mac guests and
unpaced HTTP on two. The real Linux RetroBoot importer/profile also cold boots;
native HTTP ZIP and FTP expanded payload hashes match after clean shutdown.
8.6 uses completed retail installer core plus native app installs; a complete
multi-stage CD installer run, native ATA disk or multiple auxiliary Virtio volumes
is not claimed. VM Off is directly verified in About This Computer.

The 8.6 profile uses G3/128 MiB, Virtio startup, ADB, real time, RTL8139 with opt-in
1 ms deferred TX, and 50 ms CUDA idle IRQ. TX defaults to zero for other guests;
queue/timer migration is implemented. RX pacing and driver polling experiments
were removed. No experimental polling is in the shared loader. Tools, sharing,
tablet and sound are off in this base. RetroBoot requires verified startup loader,
CUDA idle IRQ, relative-pointer and deferred-TX emulator capabilities before listing
8.6. Older clients retain the old catalog because they cannot handle these images.

Checks: Swift 75 tests / 2 optional skips / 0 failures; desktop 23 pass / 2 skips;
public 8.6 import adds a real Swift pass and all 8 parity tests pass. Packager 3
pass. Matching Linux emulator build and capability verification pass; 43 Linux
hardware/catalog tests pass. No physical/appliance presenter qualification claimed.

Client source is local and has not been pushed, integrated or released. Existing
installed apps need companion updates to display all three bases. RetroBoot D044
emulator/guest/hostd workstreams remain owned by their coordinator; do not edit
them. No cross-task messages or physical device changes occurred. VM175 retains
its private build/proof files. All owned guest Macs shut down natively.

User approved pushing both prepared client branches and opening integration PRs.
Next: publish those PRs; the RetroBoot PR remains a draft until D044 integration
and appliance validation. This approval does not publish app binaries or deploy
physical devices. Two repeated QEMU source-preparation passes verify the RTL
reset-list fix, and both CI smoke paths now match package version 4.6.0.
