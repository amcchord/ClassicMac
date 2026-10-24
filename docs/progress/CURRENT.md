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

8.6 boots from a single writable Virtio disk on G3/128 MB, VM off. Its retail
ROM needs USB off and the RTL8139 driver for networking. An interrupt from the
NIC wakes the observed freeze: this is the known nanokernel main-task lost
wakeup. An opt-in port of RetroBoot's idle-interrupt patch (default off) now
keeps it responsive; Netscape Communicator HTTP passes. Passive FTP is set
through the native Internet panel. Transmit 1.6 stalls on a passive file
transfer; official Transmit 1.2 PPC is being tested. Full self-hosted installer
completion is being retried with the idle-wakeup fix. 8.6 is not qualified.

The qualified 7.5.5 and 8.1 immutable archives and catalog-v2.json are now public.
Full public HTTPS downloads, hashes, byte ranges, production Swift import and
Finder input on the imported copies pass. Legacy catalog.json is unchanged.
Mac OS 8.6 is not uploaded or listed. New ClassicMac needs catalog-v2.json,
Quadra archive support and a qualified 8.6 runtime profile. Keep catalog.json
unchanged because older Swift clients reject the entire catalog on Quadra.
RetroBoot D044 is independently implementing Quadra in its three active task
worktrees; do not edit them. No cross-task message has been sent while the
coordination permission question is pending. Owned VM175 is a private Linux
emulator smoke-test builder, not production appliance validation.

Next: qualify 8.6 installer/apps/storage and finish the matching launch profile;
validate real imports, prepare reviewable code atop current main, upload only
qualified immutable archives and atomically promote compatible catalogs.
