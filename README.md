# ClassicMac

A self-contained macOS (Apple Silicon) application that emulates a **68040 Macintosh Quadra 800** as performantly as possible, built on a custom build of QEMU. It is designed for teaching classic Mac OS (System 7.1 through Mac OS 8.1).

ClassicMac bundles `qemu-system-m68k` together with the enhanced **paravirtualized NuBus framebuffer** (`nubus-qfb`), which unlocks arbitrary screen resolutions and 16-bit ("Thousands") color that the stock QEMU framebuffer cannot provide. A native SwiftUI configurator manages virtual machines (disks, RAM, resolution) and launches QEMU, which renders the emulated Mac in its own fast native Cocoa window.

## Features

- Emulates a Quadra 800 (Motorola 68040), the sweet spot for Mac OS 7.1 - 8.1.
- Enhanced framebuffer (`-M q800,fb=qemu`): arbitrary resolutions up to 3840x2160, all QuickDraw color depths including Thousands (16-bit), gamma correction, and multiple monitors.
- Simple GUI for creating disk images, choosing RAM and resolution, attaching install CDs, and launching/stopping the machine.
- VM control bar: Pause / Resume, Restart, and Power Off a running machine (via QEMU's monitor).
- Custom resolutions, including a "Match Display" button that sizes the Mac to your screen.
- Fully bundled, self-contained `ClassicMac.app` for Apple Silicon (M1 or later) - no Homebrew or manual QEMU install required by the end user.

## Display & sound notes

- Color depth: the resolution and depth you choose are passed to the enhanced
  framebuffer as the *deepest available* mode. Classic Mac OS still decides the
  active depth at startup and, with a fresh system, comes up in black & white
  until you choose Thousands/Millions once in **Monitors & Sound** (or the
  Control Strip). That choice is then remembered per machine.
- Sound is **off by default**: the emulated Apple Sound Chip emits a constant
  hum when idle, so audio is routed to a silent backend unless you enable
  "Sound" on a machine.

## Requirements

- Apple Silicon Mac (M1 or later) running a recent macOS.
- For **building** from source: Xcode command line tools and [Homebrew](https://brew.sh).

## Repository layout

```
ClassicMac/
  Resources/Quadra800.rom   # bundled Quadra 800 ROM (checksum F1ACAD13)
  scripts/
    build-qemu.sh           # build qemu-system-m68k + qemu-img from the SolraBizna fork
    bundle-qemu.sh          # collect dylibs + firmware and code-sign into the .app
  app/                      # SwiftUI configurator / launcher (SwiftPM package)
```

> The Quadra 800 ROM is committed so the app is turnkey. The Mac OS 8.1 install ISO is **not** committed; import your own copy through the app on first run.

## Building

```bash
# 1. Build the emulator (clones and compiles QEMU from the SolraBizna fork)
./scripts/build-qemu.sh

# 2. Build the SwiftUI app and bundle QEMU + dependencies into ClassicMac.app
./scripts/bundle-qemu.sh
```

Both scripts are idempotent and can be re-run safely.

## Emulation notes

- m68k emulation in QEMU uses the TCG just-in-time translator. There is no hardware
  virtualization path for m68k, so performance is governed by the host CPU; an Apple
  Silicon Mac runs a Quadra 800 comfortably faster than the original hardware.
- The enhanced framebuffer firmware (`mac_qfb.rom`) is part of the QEMU fork and is
  bundled automatically; no separate driver installation is required inside the guest.

## Credits

- [QEMU](https://www.qemu.org/) and the m68k / q800 maintainers.
- [SolraBizna/qemu](https://github.com/SolraBizna/qemu) for the `nubus-qfb` paravirtualized framebuffer.
