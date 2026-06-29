# ClassicMac

A self-contained macOS (Apple Silicon) application that emulates a **68040 Macintosh Quadra 800** as performantly as possible, built on a custom build of QEMU. It is designed for teaching classic Mac OS (System 7.1 through Mac OS 8.1).

ClassicMac bundles `qemu-system-m68k` (built from mainline QEMU 11.0.2 with a ported-in enhanced **paravirtualized NuBus framebuffer**, `nubus-qfb`), which unlocks arbitrary screen resolutions and 16-bit ("Thousands") color that the stock QEMU framebuffer cannot provide. It also supports **host folder sharing** via the NuBus virtio transport. A native SwiftUI configurator manages virtual machines (disks, RAM, resolution, shared folder) and launches QEMU, which renders the emulated Mac in its own fast native Cocoa window.

## Features

- Emulates a Quadra 800 (Motorola 68040), the sweet spot for Mac OS 7.1 - 8.1.
- Enhanced framebuffer (`-M q800,fb=qemu`): arbitrary resolutions up to 3840x2160, all QuickDraw color depths including Thousands (16-bit), gamma correction, and multiple monitors.
- Simple GUI for creating disk images, choosing RAM and resolution, attaching install CDs, and launching/stopping the machine.
- VM control bar: Pause / Resume, Restart, and Power Off a running machine (via QEMU's monitor).
- Custom resolutions, including a "Match Display" button that sizes the Mac to your screen.
- Host folder sharing: a folder on your Mac appears as a disk on the emulated desktop (read/write), via the classicvirtio driver ROM and virtio-9p.
- Fully bundled, self-contained `ClassicMac.app` for Apple Silicon (M1 or later) - no Homebrew or manual QEMU install required by the end user.

## Shared folders

Pick a folder when creating a machine (or in its settings) and it mounts on the
Mac desktop as a disk. Notes:

- New machines start from a pre-seeded PRAM; the virtio declaration ROM hangs the
  boot on a blank PRAM, so sharing works reliably on machines created with this
  version of ClassicMac.
- Classic Mac resource forks and type/creator codes are stored beside each file as
  `.rdump` / `.idump` sidecars, so data files transfer perfectly and Mac files
  round-trip (leaving those sidecar files in the shared folder).

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
  qfb/                      # nubus-qfb device sources + integration patch + firmware
  shared/                   # classicvirtio declrom + PRAM seed for folder sharing
  scripts/
    build-qemu.sh           # clone mainline QEMU 11.0.2 + apply the qfb port, then build
    bundle-qemu.sh          # collect dylibs + firmware and code-sign into the .app
  app/                      # SwiftUI configurator / launcher (SwiftPM package)
```

> The Quadra 800 ROM is committed so the app is turnkey. The Mac OS 8.1 install ISO is **not** committed; import your own copy through the app on first run.

## Building

```bash
# 1. Build the emulator (clones mainline QEMU, applies the qfb port, compiles)
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

- [QEMU](https://www.qemu.org/) and the m68k / q800 maintainers (incl. the `nubus-virtio-mmio` transport).
- [SolraBizna/qemu](https://github.com/SolraBizna/qemu) for the `nubus-qfb` paravirtualized framebuffer (ported here onto QEMU 11.0.2).
- [elliotnunn/classicvirtio](https://github.com/elliotnunn/classicvirtio) for the classic Mac OS virtio driver ROM used for folder sharing.
