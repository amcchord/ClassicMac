# Classic OS base images — in progress

Task branch `codex/classic-base-images`, contained worktree `worktrees/classic-base-images`.
User requested System 7.5.5, Mac OS 8.1 and 8.6 base installs with StuffIt, Transmit,
Netscape, and explicitly authorized uploads and catalog setup for ClassicMac and
RetroBoot. Root/main and unrelated releases remain separate work.

7.5.5 and 8.1 are scrubbed, compacted, packaged immutable local v1 archives.
They have native Netscape 3.04, Transmit 1.2 and StuffIt 5.5. HTTP, passive FTP,
expansion with exact payload comparisons, Mac/Linux boot and input pass.
Production Swift checksum verification and real archive import pass for both;
those imported copies also boot. Source disk files are frozen read-only.
Artifacts/evidence are in the ignored root `output/classic-base-images`.

8.6 is still unqualified. Both PPC and 68k Transmit 1.2 can complete an
individual 4 MiB passive FTP download with automatic StuffIt expansion and
exact payload hash, but repeated fresh-image downloads also stall. Neither
switching app architecture nor avoiding filename overwrites fixes reliability.
Native About This Computer confirms actual VM Off / 128 MB. HTTP and FTP
login/LIST pass. The stalled RTL receive ring was empty with no pending IRQ.
A smaller 512 MiB HFS probe also stalls; bare HFS+ does not boot through this
firmware. A native CMD646 disk boots the retail ROM but cannot find a root
disk, but native Drive Setup sees no CMD646 drives. The G4 ASR image was decoded
with ndif2raw and mounted read-only; its System/ROM are intact, including
ROM 2.5.1 matching the reference collection. That ROM fails early in QEMU.
ROM 1.8.1 needs explicit tbxi/chrp Finder metadata (the reference file has
none); with metadata it reaches ROM code, but still has a black display.
Current probe tests Apple's documented TCP/IP Load Only When Needed setting. No 8.6 image or entry is public.

The qualified 7.5.5 and 8.1 immutable archives and catalog-v2.json are now public.
Full public HTTPS downloads, hashes, byte ranges, production Swift import and
Finder input on the imported copies pass. Legacy catalog.json is unchanged.
Mac OS 8.6 is not uploaded or listed. New ClassicMac needs catalog-v2.json,
Quadra archive support and a qualified 8.6 runtime profile. Keep catalog.json
unchanged because older Swift clients reject the entire catalog on Quadra.
RetroBoot D044 is independently implementing Quadra in its three active task
worktrees; do not edit them. No cross-task message has been sent while the
coordination permission question is pending. The separate RetroBoot task branch
now builds on its completed hostd dependency e473f4c; local commit 07aff57 changes
the compatible catalog default to v2 and records real imports of both public
68k archives. It is not integrated or released. Candidate runtime/loader capability gates
are committed locally at bec18c7; 26 pure hardware/catalog checks pass, with
Linux-dependent appliance tests deferred. The new loader reproduces all old
intermediate hashes and ends at 1aaa285a...bcf45. It is not deployed. Owned VM175 is a private Linux
emulator smoke-test builder, not production appliance validation.

Next: qualify 8.6 installer/apps/storage and finish the matching launch profile;
validate real imports, prepare reviewable code atop current main, upload only
qualified immutable archives and atomically promote compatible catalogs.
