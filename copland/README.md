# Copland D11E4

ClassicMac uses a dedicated Power Mac 7500 helper to boot the unfinished Copland
preview. Download it using **File > Download a Mac**. Keep the machine's ROM,
NVRAM and disk together inside its `.classic` package. The tested configuration
is a PowerPC 601 with 32 MB RAM and a 640×480 display.

This is experimental historical software. Finder actions and booting a modified
disk can trigger Apple's assertions. ClassicMac shows **Continue Copland** when
the debugger reports a stop. Continuing may recover it, or the affected operation
may fail. No assertion is automatically skipped. Unsaved work can be lost.

Use Control-G to capture/release the mouse, Control-+ / Control-− to scale the
window, and Control-F for full screen. Choose **Shut Down** in the guest's **Spaz**
menu for a graceful shutdown. ClassicMac's confirmed Shut Down command powers off
the preview immediately. Restart creates a fresh emulator process. Sound,
networking, folder sharing, removable media, GXMetal, text paste and browser
viewing are unavailable in this machine.

## Build and provenance

Run `scripts/build-copland.sh` before `scripts/bundle-qemu.sh`. CMake, Ninja and
Apple's command-line tools are required. The script pins DingusPPC's Copland fork
and SDL2, applies `rtc.patch` and `host-integration.patch`, installs the source
overlays, and runs the serial protocol tests. The helper links only system dynamic
libraries and targets macOS 15 on Apple Silicon.

The eleven hardware fixes are Michael Steil's work in the pinned `copland-boot`
branch. His `wasm-port` contributes the pinned RTC patch and debugger protocol.
The silent audio backend preserves the working browser port's bounded 384-frame
DMA pulls every millisecond of guest time, without an off-thread host audio
callback. Native Cubeb audio stalled during our startup tests.

- [Research and reference emulator](https://www.pagetable.com/300)
- [Pinned DingusPPC source](https://github.com/mist64/dingusppc/tree/8dcac6fb160adfd8860c2252fba321d412b2c8a6)
- [Historical Copland builds](https://winworldpc.com/product/mac-os-8/copland)

The downloadable machine is based on the D11E4 reference disk and matched 7500
ROM/NVRAM published with the reference emulator. The disk retains System 7.5.3
and free space required for Copland's virtual memory. Apple assets are not placed
in this source tree. Template import validates the archive SHA-256, exact member
sizes and ROM hash; it rejects paths, links and unsupported members.

The parent control pipe is enabled only by ClassicMac, has bounded input, and
accepts fixed commands. It exposes no network listener. EOF stops the helper.
Framebuffer previews and debugger state are published atomically. The debugger
validates checksums and packet lengths, bounds its reply queue, and resumes only
at the user's request.
