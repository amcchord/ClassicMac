# ClassicMac 3.2.0 released

Copland D11E4 boots natively in a dedicated Power Mac 7500 helper. Choose
**File → Download a Mac → Copland D11E4**. The OS 9 download remains unchanged.
Copland is experimental: later boots and Finder actions can assert or crash;
ClassicMac exposes manual **Continue Copland**. Sound, networking, shared folders,
removable media, GXMetal, text paste and browser viewing are unavailable for it.

PR [#20](https://github.com/amcchord/ClassicMac/pull/20) is merged. Release
[v3.2.0](https://github.com/amcchord/ClassicMac/releases/tag/v3.2.0) points to
`4ac4d62ff263cbdbb23177b9d71bf9986e3f1d3d`. The DMG and ZIP contain the signed,
Apple-notarized, stapled app. Exact Copland engine source and checksums are
published beside them. A fresh public DMG download matched its hash and passed
mounted-image, Gatekeeper, signature, version and dependency verification.

The live mcchord.net catalog lists Copland and retains the original OS 9 entry.
The new immutable disk archive passed a fresh HTTPS download and production
import/boot. The former catalog is backed up for atomic rollback. Prior release
assets and guest archives were not overwritten.

107 Swift tests passed, with two opt-in asset tests skipped in the ordinary
suite and passed separately against the public image and signed helper. Native
boot, pause/resume, restart, explicit assertion recovery, input, persistence and
power-off were exercised. Guest Spaz > Shut Down exited cleanly; later boots can
still need Continue. The source archive rebuilt and its protocol tests passed.
Physical macOS 15 and a final manual window walkthrough were not tested (the host
screen locked); final runtime tests used the real framebuffer/hardware APIs.

Work is retained in `worktrees/copland` on `codex/copland`. The original root
iPad checkout is preserved. All task-owned emulator processes are stopped.
See [qualification](../3.2.0-validation.md), [release notes](../releases/3.2.0.md),
[implementation record](COPLAND.md), and [hosting/rollback](../operations/classicmac-downloads.md).

No release work remains. Further Copland compatibility improvements should use
fresh disposable machines and preserve the existing download as a known bootable
baseline.
