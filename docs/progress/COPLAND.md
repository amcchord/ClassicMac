# Copland integration — September 21, 2026

Worktree: `worktrees/copland`, branch `codex/copland`, based on GitHub `main`
`467eed6`. The root iPad checkout is preserved. User authorized a Copland bootable
image on mcchord.net, GitHub push, and notarized desktop releases once working.

## Investigation

ClassicMac's existing QEMU mac99 machine is substantially newer than Copland's
supported hardware. Investigating a separate native Power Macintosh 7500 engine,
pinned to Michael Steil's DingusPPC `copland-boot` commit
`8dcac6fb160adfd8860c2252fba321d412b2c8a6` (11 Copland device fixes).
The wasm-port branch has additional portability work and a debugger serial peer.
References: https://www.pagetable.com/300 and https://winworldpc.com/product/mac-os-8/copland.

Downloaded the demo's public disk, ROM, and NVRAM into ignored
`output/copland/assets`. Raw disk SHA256:
`383aa715435e02446b98ce478d1ce5729f77e8606b6657385bacba50970c1469`.
ROM: `098b588dbe12fdfa3d388636e431ccae69cd1c6e984801267b9b2602babbfd22`.
NVRAM: `8e10943ca50855fa90d5d89cc2c8b6768d810755ca6e1bb0056727dfa4a6a2d5`.
All boots use a separate writable disk copy. No user machines are used.

The native engine compiles. SDL2 2.32.10 is built statically from
`5d249570393f7a37e037abf22cd6012a4cc56a71` for macOS 15, avoiding the
host Homebrew SDL2-compat/SDL3 binaries' macOS 27 requirement. QEMU's existing
build and transport verification also passed in this worktree.

## Working native boot and integration

Native Copland reaches its purple Finder desktop repeatedly. The decisive
change was using the wasm port's bounded, emulation-thread sound DMA handling
with host audio omitted. Cubeb/native-audio startup stalled in the microkernel.
Static SDL2 and a pinned 2027 RTC are also used.

The Power Mac 7500 family, catalog selection, strict firmware-aware import,
previews, parent control pipe, and manual debugger Continue are implemented.
RAM is fixed at 32 MB. Unsupported sound/network/sharing/media/browser controls
are hidden. Copland's host Shut Down is explicitly a forced power-off; users are
directed to its Spaz menu for guest shutdown. A fresh process handles restarts.

The serial peer bounds packets/queues and checks checksums; its C++ protocol
regression passes. The Swift suite passes 104 tests before the opt-in full-engine
lifecycle test was added. Real template import and SHA-256 validation pass.
The signed app assembles and release validation confirms all bundled runtimes
support macOS 15. The unchanged Tools CD is reused from the official v3.0.0 ZIP
(SHA ec402a3c26c9d8426f139a7f979c6cf74f2eae9a61c7e4a662162818d1779ebd).
No compiler toolchain rebuild is needed for unchanged guest additions.

Copland itself is unstable: naming a desktop folder triggers WindowRef
assertions, and a hard reset can trigger CatalogReplaceRecord/CatalogCreateRecord
assertions. The manual Continue protocol recovers the catalog stops and a folder
created in the prior run persists. Do not claim a stable OS or automatic recovery.
The first app-manager lifecycle run passed initial boot/pause/stop, but failed
its assertion-free-restart expectation. The follow-up test explicitly exercises
bounded user-requested Continue during restart recovery.

The Mac locked during testing. Asked the user to unlock for native window QA;
no reply yet. Build and emulator API tests continue independently.

## Publication state

The immutable archive is uploaded and a new public download matches its hash:
`https://mcchord.net/classicmac/copland-d11e4-v1.tar.gz`
SHA256 `371f2c085a832e3af3cf510c36c2d41c6727af78a28290bb002f6c6871230c1e`.
36,782,765 compressed bytes; 178,207,500 expanded bytes; disk 173,948,928 bytes.
The template uses pristine reference assets plus a desktop preview, not a test
machine's modified disk. Minimum app version is 3.2.0.

PR #20 contains the implementation and qualification records. The final signed
app and DMG are notarized, stapled and pass mounted-image verification. The
source archive rebuilt from scratch. 107 Swift tests pass (the two opt-in real
image cases also pass separately). The signed helper passed public-image boot,
pause/resume, restart with two manual Continue operations, and host power-off.
Guest Spaz > Shut Down exited cleanly, although its next boot still needed the
same catalog assertion recovery. Full record: `docs/3.2.0-validation.md`.

The Mac remained locked during final manual window QA; real framebuffer/input
and app-manager tests passed. PR #20 is merged as `4ac4d62`, tag v3.2.0 is pushed,
and the notarized DMG, ZIP, corresponding-source archive and checksums are public:
https://github.com/amcchord/ClassicMac/releases/tag/v3.2.0.

The live catalog now includes Copland and preserves the exact OS 9 entry.
Promotion verified old/new hashes and saved the 0600 pre-Copland server backup.
Fresh public catalog import and GitHub DMG verification passed; GitHub asset
digests match all recorded hashes. All task-owned emulators are stopped.
The root iPad checkout remains unchanged. No requested release work remains.
