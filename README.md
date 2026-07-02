# ClassicMac

A self-contained macOS (Apple Silicon) application that emulates classic Macintoshes as performantly as possible, built on a custom build of QEMU. It covers the whole classic Mac OS era with two machine models:

- **Macintosh Quadra 800** (Motorola 68040) for System 7.1 through Mac OS 8.1.
- **Power Mac G4** (PowerPC, QEMU `mac99`) for Mac OS 8.5 through 9.2.2.

ClassicMac bundles `qemu-system-m68k` (built from mainline QEMU 11.0.2 with a ported-in enhanced **paravirtualized NuBus framebuffer**, `nubus-qfb`), which unlocks arbitrary screen resolutions and 16-bit ("Thousands") color that the stock QEMU framebuffer cannot provide. It also supports **host folder sharing** via the NuBus virtio transport. The bundled `qemu-system-ppc` boots the Power Mac through OpenBIOS, so **no Apple ROM file is needed** for Mac OS 9, supports **host folder sharing** via `virtio-9p-pci` and the classicvirtio ndrvloader, and ships a custom `qemu_vga.ndrv` video driver plus a patched std VGA device that let Mac OS 9 **follow the window when you resize it** - the same live resolution switching the Quadra has. A native SwiftUI configurator manages virtual machines (disks, RAM, resolution, shared folder) and launches QEMU, which renders the emulated Mac in its own fast native Cocoa window.

## Features

- Emulates a Quadra 800 (Motorola 68040), the sweet spot for Mac OS 7.1 - 8.1.
- Emulates a Power Mac G4 (`mac99` with PMU/USB input) for Mac OS 8.5 - 9.2.2: IDE storage, sungem user-mode networking, screamer (AWACS) sound, and a std VGA framebuffer at millions of colors driven by a custom `qemu_vga.ndrv`.
- Enhanced framebuffer (`-M q800,fb=qemu`): arbitrary resolutions up to 3840x2160, all QuickDraw color depths including Thousands (16-bit), gamma correction, and multiple monitors.
- Live resolution switching on **both machines**: the guest's **Monitors** control panel offers a list of standard resolutions you can switch between without rebooting, and the QEMU window follows. You can also just **drag the QEMU window** to any size and the guest switches to that exact pixel resolution when you release the mouse (the Display Manager does the switch). On the Quadra this needs Mac OS 7.6-8.1; on the Power Mac it works throughout Mac OS 9.
- Self-contained **`.classic` machine files**: each VM is a single document (a package holding its config, disk, and PRAM) that you can keep anywhere - Documents, Desktop, an external drive - and open or boot by double-clicking in Finder.
- Simple GUI for creating disk images, choosing RAM and resolution, attaching install CDs, and launching/stopping the machine.
- VM control bar: Pause / Resume, Restart, and Power Off a running machine (via QEMU's monitor).
- Custom resolutions, including a "Match Display" button that sizes the Mac to your screen.
- Host folder sharing on both machines: a folder on your Mac appears as a disk on the emulated desktop (read/write), via the classicvirtio drivers and virtio-9p.
- Fully bundled, self-contained `ClassicMac.app` for Apple Silicon (M1 or later) - no Homebrew or manual QEMU install required by the end user.

## Virtual machine files (`.classic`)

Each machine is a self-contained **`.classic` package** - a folder that Finder
treats as a single document - holding its `config.json`, hard-disk image, and
PRAM. You can store these anywhere and move or copy them like any other file.

- **New machines** are created wherever you choose (default
  `~/Documents/ClassicMac/`).
- **Double-click** a `.classic` file in Finder to open it in ClassicMac and boot
  it; **File > Open** and **File > Open Recent** work too.
- Removing a machine offers **Move to Trash** (deletes the file) or **Remove
  from Library** (keeps the file on disk, just forgets it).
- Machines created by earlier versions (stored under
  `~/Library/Application Support/ClassicMac/VMs/`) are **migrated automatically**
  to `.classic` files in `~/Documents/ClassicMac/` the first time you launch this
  version.

## Shared folders

Pick a folder when creating a machine (or in its settings) and it mounts on the
Mac desktop as a disk. Notes:

- On the Quadra 800 the share arrives through the classicvirtio NuBus
  declaration ROM. New machines start from a pre-seeded PRAM; the virtio
  declaration ROM hangs the boot on a blank PRAM, so sharing works reliably on
  machines created with this version of ClassicMac.
- On the Power Mac the share arrives through `virtio-9p-pci`; the classicvirtio
  ndrvloader is placed in guest RAM at boot and installs the driver before Mac
  OS starts. While the machine is set to boot from CD (e.g. during an OS
  install), sharing is temporarily inactive.
- Classic Mac resource forks and type/creator codes are stored beside each file as
  `.rdump` / `.idump` sidecars, so data files transfer perfectly and Mac files
  round-trip (leaving those sidecar files in the shared folder).

## Display & sound notes

- Color depth: the resolution and depth you choose are passed to the enhanced
  framebuffer as the *deepest available* mode. Classic Mac OS still decides the
  active depth at startup and, with a fresh system, comes up in black & white
  until you choose Thousands/Millions once in **Monitors & Sound** (or the
  Control Strip). That choice is then remembered per machine.
- Resolution: the value you pick is the *boot* resolution. Once booted, the
  enhanced framebuffer driver advertises a list of standard resolutions, so you
  can switch resolution live from **Monitors & Sound** and the window resizes to
  match. The QEMU window is also freely resizable: drag it to any size and, when
  you release the mouse, the guest driver switches to that exact pixel
  resolution (the framebuffer scales to fill the window while you drag). Live
  switching relies on the Display Manager, so it needs Mac OS ~7.6 or newer;
  older systems (and A/UX) still boot fine at the configured resolution and just
  scale to the window.
- Sound is **on by default** and clean. QEMU's Apple Sound Chip emulation is
  patched (`qfb/asc-silence.patch`) to feed the audio backend silence whenever
  the Mac isn't playing anything, so the old constant idle hum/buzz is gone. You
  can still turn "Sound" off per machine to route audio to a silent backend.

### Power Mac G4 (Mac OS 9) notes

- The Power Mac boots through **OpenBIOS** (bundled with QEMU); the display
  resolution you pick (any custom size, or "Match Display") is set at startup
  and the framebuffer runs at millions of colors.
- **Live window resizing works here too**: drag the QEMU window to any size
  and Mac OS switches to that resolution when you release the mouse. Under the
  hood the std VGA device is patched with a host-resize request channel
  (`ppcvid/vga-host-resize.patch`) and the bundled `qemu_vga.ndrv` (built from
  `ppcvid/driver`, a fork of the QemuMacDrivers VGA driver) polls it from its
  VBL task, retargets a dynamic display mode, and fires a VSL connect-change
  interrupt so the Display Manager re-probes the display and adopts the new
  size - no software install inside the guest is needed. Lower
  resolutions/depths can still be chosen inside Mac OS via the Monitors
  control panel, and the window follows. Widths snap down to a multiple of 8
  (a VGA hardware constraint).
- Host folder sharing works on both machines.
- **Sound works** via the **screamer (AWACS)** device, ported from Mark
  Cave-Ayland's out-of-tree QEMU branch (`screamer/` in this repo) together
  with a screamer-aware OpenBIOS build. Mac OS 9 needs **less than 1 GB of
  RAM** for stable sound, so the app's presets stop at 896 MB.
- Storage is IDE and networking is the onboard `sungem` NIC, both supported out
  of the box by Mac OS 9.

## Requirements

- Apple Silicon Mac (M1 or later) running a recent macOS.
- For **building** from source: Xcode command line tools and [Homebrew](https://brew.sh).

## Repository layout

```
ClassicMac/
  Resources/Quadra800.rom   # bundled Quadra 800 ROM (checksum F1ACAD13)
  qfb/                      # nubus-qfb device sources + integration/cocoa patches + firmware
    driver/                 # 68k declaration ROM + driver source (built with Retro68)
  ppcvid/                   # PPC live-resize: std VGA host-resize patch + qemu_vga.ndrv
    driver/                 # PPC video ndrv source (QemuMacDrivers fork, built with Retro68)
  screamer/                 # screamer (AWACS) PPC audio device + integration patch + OpenBIOS
  shared/                   # classicvirtio declrom + ndrvloader + PRAM seed for folder sharing
  scripts/
    build-qfb-rom.sh        # build the qfb declaration ROM/driver -> qfb/mac_qfb.rom
    build-ppcvid-ndrv.sh    # build the PPC video driver -> ppcvid/qemu_vga.ndrv
    build-qemu.sh           # clone mainline QEMU 11.0.2 + apply the qfb port, then build m68k + ppc
    bundle-qemu.sh          # collect dylibs + firmware and code-sign into the .app
  app/                      # SwiftUI configurator / launcher (SwiftPM package)
```

> The Quadra 800 ROM is committed so the app is turnkey, and the Power Mac needs no Apple ROM at all (it boots OpenBIOS). Mac OS install ISOs are **not** committed; import your own copy through the app on first run.

## Building

```bash
# 1. Build the emulator (clones mainline QEMU, applies the qfb port, compiles)
./scripts/build-qemu.sh

# 2. Build the SwiftUI app and bundle QEMU + dependencies into ClassicMac.app
./scripts/bundle-qemu.sh
```

Both scripts are idempotent and can be re-run safely.

The enhanced framebuffer firmware (`qfb/mac_qfb.rom`) is committed, so a normal
build does not need the 68k cross toolchain. If you change the driver sources in
`qfb/driver/`, rebuild the ROM with:

```bash
# Builds the Retro68 m68k-apple-macos toolchain on first run (slow, into
# vendor/), then compiles qfb/driver into qfb/mac_qfb.rom.
./scripts/build-qfb-rom.sh

# Or fold it into the QEMU build:
QFB_BUILD_ROM=1 ./scripts/build-qemu.sh
```

Likewise the Power Mac video driver (`ppcvid/qemu_vga.ndrv`) is committed. If
you change the driver sources in `ppcvid/driver/`, rebuild it with:

```bash
# Adds the Retro68 powerpc-apple-macos compilers to the toolchain on first run
# and installs Apple's Universal Interfaces (downloads the MPW-GM image), then
# compiles ppcvid/driver into ppcvid/qemu_vga.ndrv.
./scripts/build-ppcvid-ndrv.sh

# Or fold it into the QEMU build:
PPCVID_BUILD_NDRV=1 ./scripts/build-qemu.sh
```

## Emulation notes

- m68k and PowerPC emulation in QEMU use the TCG just-in-time translator. There is
  no hardware virtualization path for either, so performance is governed by the host
  CPU; an Apple Silicon Mac runs both machines comfortably faster than the original
  hardware.
- The enhanced framebuffer firmware (`mac_qfb.rom`) is part of the QEMU fork and is
  bundled automatically; no separate driver installation is required inside the guest.
- The same is true of the Power Mac video driver (`qemu_vga.ndrv`): OpenBIOS hands it
  to Mac OS at boot over fw_cfg, so live window resizing needs nothing installed in
  the guest either.

## Credits

- [QEMU](https://www.qemu.org/) and the m68k / q800 maintainers (incl. the `nubus-virtio-mmio` transport).
- [SolraBizna/qemu](https://github.com/SolraBizna/qemu) for the `nubus-qfb` paravirtualized framebuffer (ported here onto QEMU 11.0.2).
- [elliotnunn/classicvirtio](https://github.com/elliotnunn/classicvirtio) for the classic Mac OS virtio drivers used for folder sharing (68k declaration ROM and PowerPC ndrvloader), and for the Retro68 ndrv link recipe used to build the Power Mac video driver.
- [QemuMacDrivers](https://github.com/qemu/QemuMacDrivers) (Benjamin Herrenschmidt, Mark Cave-Ayland) for the `qemu_vga.ndrv` Power Mac video driver that `ppcvid/driver` extends with live host-window resizing.
- [mcayland/qemu](https://github.com/mcayland/qemu/tree/screamer) for the screamer (AWACS) PPC audio device and screamer-aware OpenBIOS (ported here onto QEMU 11.0.2).
