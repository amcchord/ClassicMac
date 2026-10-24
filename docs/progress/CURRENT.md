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

8.6 now passes repeated fresh-guest 4 MiB FTP downloads and native StuffIt
expansion with exact payload hashes using opt-in RTL8139 TX deferral (1 ms).
A second fresh guest also completes a full unpaced HTTP ZIP with exact hash.
The final PPC Transmit 1.2 candidate passes two transfers; its HTTP and final
scrub/import are in progress. A third cold guest uses the final TX-only QEMU
with migration support; the receive-pacing experiment was removed. Other guests
retain the default-zero inline path. CUDA's 50 ms idle IRQ separately fixes
lost Finder wakeups. Native About This Computer verifies VM Off / 128 MB.
The source is retail installer core plus native installed applications, not a
claim of full multi-stage installer success. Native CMD646/two-Virtio installer
probes remain unqualified and are not being shipped. No 8.6 image or entry is
public yet. Linux runtime build/verification passes; actual guest check next.

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
