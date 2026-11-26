<p align="center">
  <img src="Resources/AppIcon.png" width="180" alt="ClassicMac icon">
</p>

<h1 align="center">ClassicMac</h1>

<p align="center">
  <strong>The classic Mac OS era on macOS, Windows and Linux.</strong><br>
  A self-contained desktop app that emulates a Motorola 68040 Quadra 800, PowerPC Power Mac G4<br>
  (Mac OS 8/9 and Mac OS X Tiger), and experimental Power Mac 7500 (Copland on macOS) with the Apple Platinum appearance.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-black" alt="Platform">
  <img src="https://img.shields.io/badge/guests-System%207.1%20–%20Mac%20OS%20X%20Tiger-blueviolet" alt="Guest OS range">
  <img src="https://img.shields.io/badge/QEMU-11.0.2%20%7C%2010.0.13-orange" alt="QEMU">
  <img src="https://img.shields.io/badge/UI-Apple%20Platinum-lightgrey" alt="Apple Platinum interface">
  <img src="https://img.shields.io/badge/3D-ATI%20Radeon%20(emulated)-c00" alt="Emulated ATI Radeon 3D">
</p>

---

<p align="center">
  <img src="docs/screenshots/platinum/running-ati.png" width="720" alt="ClassicMac 4.10.0 in the Apple Platinum appearance, with Mac OS 9.2.2 Mega Deluxe running on the emulated ATI Radeon">
</p>

<p align="center">
  <img src="docs/screenshots/quadra800-macos81.png" width="410" alt="Mac OS 8.1 on the emulated Quadra 800">
  <img src="docs/screenshots/powermacg4-macos92.png" width="410" alt="Mac OS 9.2.2 Mega Deluxe on the emulated Power Mac G4">
</p>

## Download ClassicMac 4.10.0

| System | Download | Requirements |
| --- | --- | --- |
| Windows | [Portable EXE](https://github.com/amcchord/ClassicMac/releases/download/v4.10.0/ClassicMac-4.10.0-windows-x64.exe) | Windows 11 x64; unsigned. |
| macOS | [DMG](https://github.com/amcchord/ClassicMac/releases/download/v4.10.0/ClassicMac-4.10.0.dmg) · [ZIP](https://github.com/amcchord/ClassicMac/releases/download/v4.10.0/ClassicMac-4.10.0.zip) | Apple Silicon, macOS 15+; signed and notarized. |
| Linux | [AppImage](https://github.com/amcchord/ClassicMac/releases/download/v4.10.0/ClassicMac-4.10.0-linux-x64.AppImage) | x64, glibc 2.39+ and a desktop. |

[Release notes, checksums and engine sources](https://github.com/amcchord/ClassicMac/releases/tag/v4.10.0).
Windows runs directly. Drag the Mac app to Applications. On Linux, mark the
AppImage executable; use `--appimage-extract-and-run` if FUSE is unavailable.

## Related project: RetroBoot

ClassicMac is a sister project to **[RetroBoot](https://github.com/amcchord/RetroBoot)** — the two are very similar and share much of the same custom QEMU classic Mac emulation stack, guest drivers, Apple Platinum UI, and ready-to-run Mac OS image library.

- **Choose ClassicMac** if you want to run classic Mac OS in a window on your everyday **Apple Silicon Mac, Windows 11 PC, or Linux desktop** (or **iPad** via the beta) alongside modern apps, keep portable `.classic` machine files, or try experimental **Copland** on macOS.
- **Choose RetroBoot** if you want to turn a **spare PC, Intel Mac, Raspberry Pi, or Planet Computers Gemini PDA** into a dedicated, boot-straight-to-Mac-OS appliance with a startup chime and no host desktop — ideal for kids, families, or a standalone retro machine with gamepad support, plug-and-play USB drives, and phone/browser management over your local network.

## User Manual & Complete Documentation

See the full **[ClassicMac 4.10.0 User Manual](docs/manual/README.md)** (12 chapters with architecture diagrams, configuration schemas, and screenshots):

- **Getting started & requirements:** [System Requirements](docs/manual/01-system-requirements.md) · [Architecture & How It Works](docs/manual/02-architecture-how-it-works.md) · [Installation & First Run](docs/manual/03-installation-and-first-run.md)
- **Operating systems:** [Mac OS Library & Editions](docs/manual/04-mac-os-library-and-editions.md) · [Creating Custom Macs & Manual OS Installs](docs/manual/05-installing-your-own-mac-os.md) · [Everyday Use & Input](docs/manual/06-everyday-use-and-input.md)
- **Devices & I/O:** [Display, 3D Graphics & Sound](docs/manual/07-display-3d-graphics-and-sound.md) · [Media Drawer, Tools CD & Shared Folders](docs/manual/08-media-tools-and-sharing.md) · [Networking & Virtual Printing](docs/manual/09-networking-and-virtual-printing.md)
- **Hosting & reference:** [Creating & Serving Custom Images](docs/manual/10-creating-and-serving-images.md) · [Checkpoints, Updates & Troubleshooting](docs/manual/11-checkpoints-updates-and-troubleshooting.md) · [Configuration, Bundles & Env Reference](docs/manual/12-configuration-and-cli-reference.md)

## New in ClassicMac 4.10.0

- **Virtual printing to host printers or PDF.** Ported from RetroBoot (off by
  default): enable per Mac in **Get Info → Printing** (macOS) or **Get Info**
  (Windows/Linux) to print via LPD (`515`), IPP (`631`), or JetDirect (`9100`)
  to your computer's **System Default** printer, a chosen host printer, or
  **Save as PDF**. On macOS, Power Mac G4 hard-disk boots automatically
  provision a `ClassicMac Printer` Desktop Printer for Mac OS 9 when enabled.
- **Easier Mac deletion.** Delete any Mac directly from the **Delete…** button
  on Home (or **⌘⌫** on macOS) with a single confirmation showing its on-disk
  size and optional stop-and-trash for running Macs.

## Download ClassicMac 4.9.0 (previous)

## New in ClassicMac 4.9.0

- **In-app software updates.** Check for updates from the app and install
  verified releases in place across macOS (Developer ID + SHA-256), Windows
  portable EXE, and Linux AppImage.
- **Zero-cost Checkpoints.** Instant APFS/reflink machine snapshots with
  automatic Fresh Install and pre-restore/pre-recovery backups.
- **Mac OS X reliability & clean shutdown.** Apple-menu verified Shut Down &
  Restart for Jaguar/Panther/Tiger, flash NVRAM & RTC sync, Screamer OS X codec
  and PMU sleep flags, plus `recommendedSettings` RAM provisioning.
- **Classic shared folders, IDE & input fixes.** Ports RetroBoot's
  `classicvirtio` folder move/name/time/Finder-flag fixes, IDE `SRST` no-IRQ
  fix, USB HID `wheel-per-report=1` scroll pacing, DPI-normalized relative
  mouse tracking, macOS `thinArchive` support, 30 s catalog timeouts, and
  sliding-window transfer speed/ETA.

## Download ClassicMac 4.8.1 (previous)

## New in ClassicMac 4.8.1

- **Radeon 32-bit texture fix.** Fixes 32-bit `ati-vga` textures rendering green
  on 16-bit desktops across QEMU 11 (PR #41) and QEMU 10 OS X.
- **Mac OS 8.6 multi-Virtio storage & folder sharing.** Fixes simultaneous
  hard disk, ClassicMac Tools, CD, Shared Folder, and tablet startup on Mac OS
  8.6 without drive collisions, Finder hangs, or OpenBIOS dictionary overflow.
- **System 7.5.x disk size & Quadra floppy startup.** Switches Quadra `scsi-hd`
  to `IBM OEM DFMSS4F` with Apple mode-page fixes so 2 GB+ drives initialize cleanly,
  and adds explicit **Floppy** startup selection (`bootFromFloppy`) via Quadra
  PRAM `DefStartRec`.
- **MIT license.** Adds a root MIT `LICENSE` covering ClassicMac's original code
  and interop patches.

## New in ClassicMac 4.8.0

- **RetroBoot 3.0.0 parity and GNU sparse downloads.** Adds RetroBoot public
  library editions (`System 7.5.5`, `Mac OS 9.2.1`, `Mac OS X 10.4.11`),
  GNU sparse 1.0 extraction (`CMachineArchive`), and engine/driver updates
  (`macio-ide.pio-status-drain`, `pci-ohci.idle-frames`, QFB snapshot
  consistency, Screamer DMA/reset fixes, and `classicvirtio` folder dates and
  directory cache).
- **Library search and clearer edition metadata.** Search and filter editions,
  pick custom save destinations with upfront sparse storage validation, and see
  accurate category breakdowns across macOS, Windows, and Linux.
- **UI, display, and reliability polish.** Hardens browser display WebSocket
  authentication and private runtime sockets, adds Mac quit force-stop for OS X
  guests, tightens modal blocking and accessibility across Platinum UI, and
  improves audio fallback and Linux/Windows runtime isolation.

## New in ClassicMac 4.7.0

- **PowerPC Mac OS X Tiger.** Choose **Power Mac G4 — Mac OS X** in
  New Mac and attach your own PowerPC installation disc image.
- The OS X profile uses its own firmware, persistent startup memory,
  native USB input, sound and standard VGA display. Use the Apple menu
  inside OS X to shut down or restart.
- Classic Macs continue to use the separate QEMU 11 engine.

[OS X setup and engine provenance](osx/README.md).

## New in ClassicMac 4.6.4

- **Radeon cursor correction.** Imports RetroBoot 2.0.2's cursor fix so the
  pointer clips correctly at screen edges and updates without stale pixels.
- **Shared graphics checks.** Mac, Windows and Linux builds exercise actual
  Radeon cursor pixels, both monochrome planes, color modes and clipping.
- **Mac startup.** Separate tablet and network interrupts when ATI graphics
  and Tools are enabled, keeping all selected devices available.

See the [4.6.4 validation report](docs/4.6.4-validation.md).

## New in ClassicMac 4.6.3

- **Library browsing on Windows and Linux.** Search and filter current
  editions, inspect hardware and sizes, name a Mac, pause/resume downloads,
  and start it directly after installation.
- **Machine management.** Remove a list entry while keeping its files, or move
  the machine folder to Recycle Bin/Trash. Compact windows keep actions visible.
- **Power and recovery.** Reach shutdown, restart and confirmed Force Stop
  from Home, the guest window and its close action. Hung engines stop without
  waiting for their control connection.
- **Linux Power Mac startup.** Separate conflicting tablet/network interrupts
  when ATI graphics and Tools are enabled, preserving the selected devices.
- **Ubuntu desktop testing.** The actual AppImage is tested in Ubuntu 24.04
  with real Library downloads, guest input and repeated performance measurements.
  See the [Ubuntu report](docs/ubuntu-validation-2026-11-15.md).

## New in ClassicMac 4.6.2

- **Updated RetroBoot engine.** Imports RetroBoot 2.0.1 CPU, display and timing
  fixes, including relative Power Mac VBL and Quadra reset behavior.
- **Measured classic controls and fonts.** Native Mac captures now verify 672
  Mac Roman glyphs/advances, popup menus, focus rings, dialogs and scrollbars.
- **Windows ATI startup fix.** Separates conflicting tablet/network interrupts
  when the Tools CD is enabled. Default configurations pass the actual five-DPI
  Windows startup, input, restart and shutdown flows.
- **Sharp oversized displays.** Scroll a large guest at whole-pixel zoom;
  choose Fit Window explicitly for fractional scaling. Guest full screen hides
  host controls, and narrow action bars wrap.
- **Whole-pixel interface zoom.** The Platinum interface snaps to 1×, 2× and
  larger whole zoom levels so title-bar stripes, text and bevels stay even at
  fractional Windows display scales.
- **ATI and Quadra change their actual resolution.** Resizing the window now
  changes the Mac desktop through its video driver, including repeated changes,
  full screen and Windows display scaling. Quadra requires Enhanced Video.
- **Safer Quadra mode switches.** A bounded gray-screen fill fixes memory
  corruption at awkward widths and color depths. Requests survive startup.
- **ATI keeps accelerated graphics.** A firmware display service programs real
  Radeon modes without installing files in the guest. Modes range from
  640 × 480 through 1920 × 1440, with widths aligned to eight pixels.

## New in ClassicMac 4.6.1

- **Windows display scaling.** Fractional scales and Per-Monitor V2 awareness
  keep the interface sized correctly. Windows and dialogs fit scaled desktops.
- **Live guest resizing.** Standard Power Macs change resolution with the
  window, including repeated changes and full screen. ATI and Quadra displays
  fit the window automatically while keeping their Mac OS video mode.
- **Sharp guest pixels.** Physical-pixel alignment avoids clipped rows at
  fractional DPI. The remembered scale menu offers sharp, 1×/2× and fit modes.
- **More reliable controls.** Mouse releases survive focus changes, capture
  guidance stays outside the guest image, and dialog actions remain accessible.

## New in ClassicMac 4.6

- **Native Windows and Linux apps.** Compiled C++ replaces Electron entirely,
  with direct guest display/input and the Mac app's measured Platinum pixels.
- **Single executable downloads.** A portable Windows EXE and a Linux AppImage
  include the native app, engines and runtime. No installation is required.
- **More classic base Macs.** The Library adds System 7.5.5, Mac OS 8.1 and
  Mac OS 8.6, including their prepared hardware and applications from PR #36.
- **Sparse virtual disks on Windows.** Empty disk space stays unallocated.
  Existing `.classic` machines and Library preferences remain compatible.

## New in ClassicMac 4.5

- **Single-file desktop downloads.** Run the portable Windows EXE directly.
  On Linux, mark the AppImage executable and run it. The optional DEB remains.
- **The Mac app’s Platinum pixels.** Shared measured control artwork and bitmap
  text replace the approximate CSS theme. The window has classic close, zoom,
  resize and WindowShade controls, with no host menu bar.
- **The same home layout.** Macs, preview, About This Mac and power controls
  follow the Mac app. New Mac, Open Mac, Library and About stay in the window.
- **Measured output.** Native framebuffer fixtures and actual app captures
  check control pixels at 1× and 2×, with zero differing pixels.

## New in ClassicMac 4.4

- **Apple's graphics drivers on emulated ATI hardware.** The clean Mac OS 9
  download now includes Apple's ATI Radeon software and starts on the emulated
  Radeon R100, like Deluxe and SuperDeluxe. RAVE and OpenGL games use the
  native Mac OS drivers.
- **A clean graphics stack.** Retired GXMetal components, catalog metadata and
  driver handoff are removed. New downloads use a freshly built image;
  existing machines keep their files and settings.

## New in ClassicMac 4.3

- **Guided first launch.** A four-step wizard covers software licenses,
  a pre-installed download or your own CD, preparation, and first startup.
- **Reliable first boot.** Windows and Linux can start without sound for the
  session when the host has no usable audio output.

## New in ClassicMac 4.2

- **Original Mac OS 9 icons.** Native Icon Services CD, floppy, folder,
  clipboard and other artwork replaces the drawn substitutes. Original
  16/32-pixel strikes and 8-bit alpha are preserved on every desktop.
- **A closer Platinum VM window.** Native Appearance Manager captures verify
  full active/inactive/WindowShade frames, held title boxes, resize corners
  and toolbar button edges at 1× and 2× with zero differing pixels. Compact
  controls keep narrow VM windows usable. [Fidelity scope](docs/platinum-fidelity-audit.md).

## New in ClassicMac 4.1.1

- **Quake III mouse controls.** Power Macs provide a USB mouse for
  InputSprocket games. Click the game screen to capture; Control–Option on
  macOS or Escape on Windows/Linux releases it. Finder keeps seamless input.
- **Smooth id intro playback.** Radeon startup initializes the synchronization
  state expected by the classic Mac OpenGL driver, avoiding repeated waits.
- **Reliable Power Mac audio.** DMA completion follows the sample clock and
  keeps the guest's sound-buffer interrupts separate.

## New in ClassicMac 4.1

- **Windows and Linux desktop editions.** Native x64 QEMU runtimes and a shared
  Platinum desktop front end, with `.classic` machine creation/import,
  settings, CD/floppy images, power controls and an authenticated local display.
- **The Library on every desktop.** Ready-to-run Mac OS 9 editions download
  with pause/resume, exact size/SHA-256 checks and bounded archive extraction.
- **Paste Text on every desktop.** Review plain text before typing it with the
  Mac US keyboard layout; unsupported characters are rejected before typing.
- **Linux folder sharing.** A selected host folder mounts through classicvirtio
  9p. Windows does not provide this QEMU backend; Copland remains Mac-only.
- **Reference-tested Platinum controls.** The Mac appearance corrects system
  font widths, raw grays, pressed/disabled controls, popup arrows and title-box
  artwork. [The audit](docs/platinum-fidelity-audit.md) documents the 32 control
  references and 95 system glyphs, and the remaining whole-app fidelity gaps.

| Download | Host | Install |
| --- | --- | --- |
| macOS DMG | Apple Silicon, macOS 15+ | Drag ClassicMac to Applications |
| Windows EXE | Windows 11, x64 | Run the single portable executable |
| Linux AppImage | Ubuntu 24.04+/Debian 13, x64 | Mark the single file executable, then run it |

The [latest release](https://github.com/amcchord/ClassicMac/releases/latest)
contains the applications, checksums and corresponding engine sources. The
macOS app is Developer ID signed and notarized. Windows binaries are unsigned.
See the [desktop build and platform support guide](desktop/README.md).

<p align="center">
  <img src="docs/screenshots/desktop/windows.png" width="410" alt="ClassicMac on Windows 11">
  <img src="docs/screenshots/desktop/linux.png" width="410" alt="ClassicMac on Linux">
</p>

## New in ClassicMac 4.0

- **Apple Platinum, top to bottom.** Every ClassicMac window is now drawn in
  the Mac OS 8/9 Platinum appearance it runs: pinstriped title bars with
  close, zoom and WindowShade boxes, a grow box, movable modal dialogs, the
  default-button ring that flashes when you press Return, beeping caution
  alerts, Platinum pop-up menus that open with the current item under the
  pointer, lavender lists and scroll bars, and the barber-pole progress bar.
  Control artwork uses one point per Mac pixel and is tested at 1× and
  2×. Some SwiftUI text and other states still need fidelity work. The look, fonts and icons are shared with RetroBoot through the
  new `PlatinumKit` module, which also builds for iPad.
- **Your Mac in a Platinum window.** Each running Mac's screen now appears
  in a ClassicMac Platinum window instead of QEMU's: sharp whole-number
  scaling, a close box that shuts Mac OS down safely, zoom and WindowShade,
  a Standard-card screen that follows the window's size, and a toolbar with
  Shut Down, Restart, Pause, Media, Paste Text and Full Screen. From
  power-on it shows the classic gray checkerboard with a startup Mac until
  Mac OS draws its own screen, so firmware black and OpenBIOS yellow never
  appear. QEMU hands its screen over through the new `-display classicmac`
  backend, ported from RetroBoot (see [`display/README.md`](display/README.md)).
- **The Mac OS Library.** **File → Mac OS Library…** (⇧⌘L) lists every
  ready-to-run Mac with what is inside it: Mac OS 9.2.2 SuperDeluxe (54
  games, ClarisWorks, Home Page), Deluxe (16 games, After Dark, music,
  benchmarks), a clean Mac OS 9.2.1 and Copland. Downloads resume, verify and
  show a classic copy-progress dialog.
- **3D through an emulated ATI Radeon, with no custom drivers.** Power Macs
  can use an emulated ATI Radeon (R100, PCI "Radeon Mac Edition") that Mac OS
  9's own ATI software drives, so QuickDraw 3D RAVE and OpenGL games render on
  your Mac's GPU. The Library's OS 9 Macs start on it.
- **Get Info** replaces Settings, with General, Display and Sharing panes;
  **About This Mac** in the main window shows the GPU drawing a running Mac's
  3D.
- **Classic icons by The Iconfactory.** The Power Mac G4, Quadra 800 and
  Power Mac 7500 use Iconfactory artwork included with permission. No extra
  download is needed. **ClassicMac → Iconfactory Icons…** credits the artists
  and lets you add more collections from your own copies.

<p align="center">
  <img src="docs/screenshots/platinum/guest-window.png" width="720" alt="Mac OS 9.2.2 Mega Deluxe in ClassicMac 4.10.0's Platinum window with the toolbar along the bottom">
</p>

<p align="center">
  <img src="docs/screenshots/platinum/main-window.png" width="410" alt="ClassicMac 4.10.0 Home window with Apple Platinum chrome">
  <img src="docs/screenshots/manual/guest-osx-tiger.png" width="410" alt="Mac OS X 10.4.11 Tiger running inside ClassicMac 4.10.0">
</p>

<p align="center">
  <img src="docs/screenshots/platinum/library.png" width="410" alt="The Mac OS Library">
  <img src="docs/screenshots/platinum/get-info-display.png" width="410" alt="Get Info, Display pane with the graphics card choice">
</p>

## Copland preview

ClassicMac 3.2 adds a downloadable **Copland D11E4** machine, Apple's unfinished
Mac OS 8 rewrite. Choose **File → Download a Mac**, select Copland, and start it.
A dedicated Power Mac 7500 engine boots the included disk with 32 MB RAM.

![Copland D11E4 running in ClassicMac](docs/screenshots/copland-d11e4.png)

This is experimental: some Finder actions trigger developer assertions or
crashes. Use **Continue Copland** in ClassicMac when it reports an assertion.
Sound, networking, shared folders, removable media and browser viewing are
unavailable. See [Copland setup, controls and source credits](copland/README.md).

## New in ClassicMac 3.0

- **Download a ready-to-run Mac OS 9.** Choose **File → New Machine** and
  the recommended **Download a Mac** option
  for a clean Mac OS 9.2.1 machine ready to start.
  Downloads support pause/resume, integrity checks, and a choice of name and
  save location. Each download creates its own independent machine.
- **A simpler machine home.** Start or return to your Mac from its preview;
  hardware, display, and sharing options live in a separate Settings window.
- **Paste Text into Mac.** Review and edit text before typing it into the
  guest, with progress and cancellation. Available from the app, native guest
  menu, and browser toolbar; requires the guest's US keyboard layout.
- **One media window.** Manage discs, Tools, recent images, and startup choice
  from the library or guest. Power Mac changes are saved for the next startup;
  Quadra floppy eject waits for the guest to finish its writes.

See the [3.0 validation report](docs/3.0-validation.md) for historical test results and known limits.

## iPad beta

ClassicMac now has an iPad-only beta for iPadOS 14 and newer. It can create and
run both Quadra 800 and Power Macintosh G4 machines entirely on-device, import
existing machine packages, and uses the same ClassicMac icon and focused
Classic Mac OS setup flow.

The iPad build uses UTM SE's App Store-compatible QEMU threaded-code
interpreter because iPadOS cannot launch the macOS app's bundled emulator
processes. That makes it slower than the Apple Silicon Mac build. This first
beta also uses upstream UTM display hardware, so the emulated ATI Radeon and custom `nubus-qfb` display device are unavailable. See
[`ios/README.md`](ios/README.md) for the pinned build and TestFlight workflow.

## 3D games: the emulated ATI Radeon

`Mac OS 9 game → ATI's own Mac OS 9 drivers → emulated Radeon R100 in QEMU → OpenGL on your Mac's GPU`

Choose **Get Info → Display → ATI Radeon (3D acceleration)** for a Power
Mac, or download a Deluxe Mac from the Library, which starts on it. The card
is QEMU's `ati-vga` with RetroBoot's R100 model: register file, display
modes and EDID, command processor, 2D engine and a 3D engine whose draws run
on a host OpenGL 4.1 thread (Apple's OpenGL over Metal). Mac OS 9's stock
*ATI Driver Update*, *ATI Radeon 3D Accelerator*, *ATI Resource Manager* and
*OpenGLRendererATI* do the rest; nothing is installed from ClassicMac. On an
Apple M5 Max the Quake III Arena demo runs at about 150 frames per second.

- Mac OS starts at 640 × 480 and switches to the size you chose (640 × 480
  up to 1920 × 1440, widths aligned to eight pixels) during startup through
  RetroBoot Display (in the Library's Macs) or ClassicMac Display (on the Tools
  disk), then follows host window resizes through the firmware PCI display
  service and ATI driver; colors are 256, Thousands or Millions.
- A Mac without ATI's software still starts on the card, at 640 × 480 with
  no 3D. Installer and CD starts always use the Standard card.
- The Standard card remains the default: any size, live window resizing and
  every color depth.

See the [engine report](docs/engine-4.0-ati.md) for the design, provenance,
argument sets and test evidence. Earlier graphics implementations remain
available in Git history and older releases.

## What it is

ClassicMac wraps custom QEMU builds (and DingusPPC for Copland on macOS) in a native Platinum app covering the classic Mac OS era and PowerPC Mac OS X Tiger across four machine profiles:

| Machine | CPU / Engine | Runs | Highlights |
| --- | --- | --- | --- |
| **Macintosh Quadra 800** | Motorola 68040 (QEMU 11) | System 7.1 – Mac OS 8.1 | Enhanced paravirtualized framebuffer (`nubus-qfb`), writable removable floppy images, any resolution up to 3840×2160, all QuickDraw depths incl. Thousands |
| **Power Mac G4** | PowerPC `mac99` (QEMU 11) | Mac OS 8.5 – 9.2.2 | OpenBIOS firmware, screamer (AWACS) sound, live resizing `qemu_vga.ndrv`, emulated ATI Radeon 3D acceleration |
| **Power Mac G4 — Mac OS X** | PowerPC `mac99` (QEMU 10) | Mac OS X 10.4 Tiger | Dedicated OS X engine, persistent Core99 flash startup memory, native USB input, widescreen standard VGA |
| **Power Mac 7500** *(macOS only, experimental)* | PowerPC 601 (DingusPPC) | Copland D11E4 | Dedicated experimental engine for Apple's unfinished Mac OS 8 rewrite |

Everything is bundled into a single self-contained download (`ClassicMac.app` on macOS, portable EXE on Windows, AppImage on Linux): the QEMU system emulators, firmware, guest video drivers, folder-sharing drivers (macOS/Linux), and a guest-additions Tools CD. No Homebrew or separate dependencies to hunt down, and nothing required inside the guest.

You bring your own Mac OS installation media: every classic Mac OS version is available in the [WinWorld operating system library](https://winworldpc.com/library/operating-systems), and ClassicMac imports the disc image with a couple of clicks. Mac OS 8.5, 8.6, 9.2.1, and the [Mac OS 9.2.2 Universal Install CD](https://macintoshgarden.org/apps/mac-os-922-universal) all boot reliably on the Power Mac — see the note in Getting started.

## Built on the shoulders of giants

ClassicMac exists because of years of brilliant work by other engineers. The patches and drivers that make it possible deserve top billing:

- [QEMU](https://www.qemu.org/) and the m68k / q800 maintainers (incl. the `nubus-virtio-mmio` transport) — the emulation core underneath everything.
- [SolraBizna/qemu](https://github.com/SolraBizna/qemu) for the `nubus-qfb` paravirtualized framebuffer (ported here onto QEMU 11.0.2) that gives the Quadra arbitrary resolutions and rich color.
- [elliotnunn/classicvirtio](https://github.com/elliotnunn/classicvirtio) for the classic Mac OS virtio drivers used for folder sharing and Quadra floppy images (68k card firmware and the PowerPC ndrvloader), and for the ndrv link recipe used to build the Power Mac video driver.
- [QemuMacDrivers](https://github.com/qemu/QemuMacDrivers) (Benjamin Herrenschmidt, Mark Cave-Ayland) for the `qemu_vga.ndrv` Power Mac video driver that `ppcvid/driver` extends with live host-window resizing.
- [mcayland/qemu](https://github.com/mcayland/qemu/tree/screamer) for the screamer (AWACS) PPC audio device and screamer-aware OpenBIOS (ported here onto QEMU 11.0.2).
- [Retro68](https://github.com/autc04/Retro68) for the 68k and PPC classic Mac OS cross toolchain.
- [noVNC](https://github.com/novnc/noVNC) for the browser-native VNC client used by the private local display page.

## Features

- **Native window or optional browser viewing.** Each machine opens in its own native Platinum window by default, with resize, full-screen, input, and removable-media controls. On macOS, turn on **Show the Mac in a web browser** under **Get Info → Display** when a private loopback browser display is more convenient; Fit, Actual Size, Full Screen, modifier-key, and game-mouse controls stay close at hand there.
- **Enhanced video on the Quadra** (`-M q800,fb=none -device nubus-qfb`): arbitrary resolutions up to 3840x2160, every QuickDraw depth including Thousands (16-bit), gamma correction, and multiple monitors — far beyond what stock QEMU's macfb can do.
- **Zero guest setup on the Power Mac.** It boots through OpenBIOS firmware, and a custom `qemu_vga.ndrv` is handed to Mac OS over fw_cfg at boot, so live resizing and millions of colors work with nothing installed in the guest.
- **Host folder sharing on classic Macs (macOS and Linux).** Pick a folder on a Quadra 800 or classic Power Mac G4 and it mounts on the emulated desktop as a read/write disk (classicvirtio + virtio-9p). Resource forks and type/creator codes round-trip via `.rdump`/`.idump` sidecars. (Windows QEMU, OS X, and Copland do not provide 9p sharing.)
- **Writable floppy images on the Quadra.** Attach a raw `.img`, `.dsk`, `.ima`, or `.raw` image in Media settings before startup and it appears as a writable classic Mac disk.
- **Clean, working sound.** The Quadra's Apple Sound Chip is patched to feed silence when idle (no more idle buzz), and the Power Mac gets the screamer (AWACS) device with a screamer-aware OpenBIOS.
- **Self-contained `.classic` machine documents.** Each VM is a single Finder package holding its config, disk, and PRAM. Keep it anywhere, double-click to boot, move it between Macs.
- **Classic input helpers.** Secondary click opens contextual menus as Control+click, and the scroll wheel becomes arrow-key taps. Turn both off in machine settings for guests with real drivers (e.g. USB Overdrive).
- **A guest-additions Tools volume** (StuffIt Expander, Disk Copy, USB Overdrive, Transmit, Lido, patched HD SC Setup...) built from `guestcd/manifest.tsv`. On Power Macs it mounts automatically at startup as a read-only Virtio disk, using the same guest-driver path as folder sharing instead of unreliable OS 9 IDE hot-plug. Everything is pre-expanded and ready to run.
- **3D on the emulated ATI Radeon.** Mac OS 9's own ATI drivers drive an
  emulated Radeon R100 whose 3D engine renders through your Mac's GPU, so RAVE
  and OpenGL games such as Quake III, Nanosaur and Cro-Mag Rally run
  accelerated with nothing installed from ClassicMac.
- **An Apple Platinum interface.** Pixel-exact Mac OS 8/9 windows, controls,
  menus, dialogs and alerts, with original MIT fonts and icons shared with
  RetroBoot.
- **The Mac OS Library.** Ready-to-run Macs with games and classic software,
  described edition by edition, downloaded with resume and integrity checks.
- **Native machine control.** Pause / Resume, Restart, and Shut Down from the app, live screen previews in the library, a visible browser URL, and a "Match Display" button that chooses a screen-sized boot resolution.
- **Safe, faster shutdown cycles.** The app's Shut Down command presses the
  virtual Mac's Power key and confirms Mac OS's own dialog, allowing HFS/HFS+
  to unmount cleanly so the next boot does not pay the recovery penalty.
- **Signed, notarized, stapled DMG** for distribution — recipients get a clean Gatekeeper experience even offline.

## Getting started

1. Grab **ClassicMac.dmg** from the [latest release](https://github.com/amcchord/ClassicMac/releases/latest), drag ClassicMac to Applications, and launch it.
2. The first-run wizard starts with a reminder: this is 30-year-old software,
   and you are responsible for having the right licenses. Acknowledge it,
   then choose **Download a pre-installed Mac** or **Install my own copy of
   Mac OS**. Downloads need no OS installation; your own installer needs a
   model and CD image. The wizard uses recommended hardware settings.
3. Choose **Start Mac** or **Start Installer** on the ready screen. Its screen opens in a
   Platinum window with a toolbar for common commands; **Show Mac** brings it
   forward. Every key, Command combinations included, goes to the Mac while
   its screen has focus; Control-Command-F toggles full screen, and games
   that capture the mouse release it with Control-Option. To use your browser instead, turn on
   **Get Info → Display → Show the Mac in a web browser** before startup.
4. Use **Media…** to attach discs and **Get Info** (⌘I) for memory, display,
   graphics card and a shared folder, which appears in the guest as a
   writable disk.

You can add more Macs later through **File → Mac OS Library…** (⇧⌘L).
The current source reads the ClassicMac and [RetroBoot public image catalogs](https://mcchord.net/retroboot-downloads/#macs).
It imports standard and sparse archives for Quadra, Classic PowerPC and PowerPC Mac OS X.
For a manual installation, choose **File → New Mac…** (⌘N): a Quadra 800
(System 7.1–8.1) or Power Mac G4 (Mac OS 8.5–9.2.2), memory, a virtual disk up
to 120 GB and your Mac OS CD image. The Mac starts from the CD; once Mac OS is
installed it starts from its hard disk by itself. To use the ATI Radeon card
on a Mac you installed yourself, install ATI's Mac OS 9 drivers from your
Mac OS 9.2 CD and ClassicMac Display from the Tools disk.

New Power Mac disks default to 32 GB. New disks use sparse storage: their
capacity is available to the classic Mac, while the file on your modern Mac
starts small and grows as data is written. The Library shows the download
size, virtual disk capacity, and free space needed separately. Existing
machines keep their current disk sizes.

> [!IMPORTANT]
> **Installing Mac OS 9?** Initialize the destination with Drive Setup from
> the CD first. In the Installer's final **Install Software** screen, open
> **Options…** and turn off **Update Apple Hard Disk Drivers**, then perform a
> regular installation. ClassicMac's QEMU build fixes the MacIO IDE/DBDMA
> completion race that made some 9.2.1 and 9.2.2 installers freeze around
> "About 4 minutes remaining," so a frozen partial System Folder is no longer
> expected or considered a successful install. During a Power Mac CD boot,
> ClassicMac leaves the Tools tray empty so the selected startup disc boots
> cleanly. Tools stays deferred for that installer boot; after installation,
> restart or shut down. ClassicMac detects the blessed System Folder and makes
> the hard disk the next startup device automatically, while leaving the disc
> inserted. The read-only **ClassicMac Tools** volume then mounts automatically.

New machines are created as `.classic` documents (default `~/Documents/ClassicMac/`). Double-click one in Finder to boot it.

Requirements:
- **macOS:** Apple Silicon Mac (M1 or later) running macOS 15 or newer.
- **Windows:** Windows 11 x64 (single portable EXE, unsigned).
- **Linux:** x64 desktop with glibc 2.39+ (Ubuntu 24.04+ / Debian 13+) on X11 or Wayland (single AppImage).

## Display & sound notes

- The resolution you pick is the *boot* resolution and the depth is the *deepest available* mode; classic Mac OS chooses the active depth at startup (a fresh system comes up in B&W until you pick Thousands/Millions once in Monitors — it's remembered per machine).
- Machines use ClassicMac's native Platinum guest window by default on every platform. On macOS, the optional **Show the Mac in a web browser** toggle is under **Get Info → Display** and takes effect on the next start.
- The browser's **Fit** mode prefers the largest whole-number scale that fits the tab and uses sharp nearest-neighbor edges when it must shrink the image. Choose **Actual Size** for one guest pixel per browser pixel, and use the page's **Fullscreen** button to enter or leave browser fullscreen.
- The browser toolbar provides sticky Command, Option, and Control modifiers plus a dedicated Escape button, which makes host-reserved key combinations practical. Games that ask for relative mouse movement reveal a **Capture game mouse** control; press Escape to leave pointer lock.
- Power Mac widths snap down to a multiple of 8 (a VGA hardware constraint).
- The Power Mac's packed low-bpp patch adds Black & White, 4 and 16 colors to the Monitors panel alongside 256/thousands/millions.
- Mac OS 9 wants **less than 1 GB of RAM** for stable sound, so the app's presets stop at 896 MB.
- On the Quadra, folder sharing arrives through the classicvirtio NuBus card firmware (new machines start from a pre-seeded PRAM so it boots reliably). On the Power Mac it arrives through `virtio-9p-pci` and the classicvirtio ndrvloader placed in guest RAM at boot; while booting from CD (e.g. an OS install) sharing is temporarily inactive.

## Building from source

```bash
# 1. Build the emulator (clones mainline QEMU 11.0.2, applies the ClassicMac
#    patch set and the ATI Radeon R100 last, compiles qemu-system-m68k +
#    qemu-system-ppc, and checks the host OpenGL path) and the Copland engine
./scripts/build-qemu.sh
./scripts/build-osx-qemu.sh
./scripts/build-copland.sh

# 2. Build the guest-additions Tools CD (cached downloads, hfsutils/unar),
#    including ClassicMac Display for the ATI card
./scripts/build-guest-cd.sh

# 3. Build the app and bundle QEMU + firmware + dylibs into
#    dist/ClassicMac.app (code-signed). APP_VERSION, APP_BUILD_VERSION and
#    DIST_DIR override the version and output folder.
./scripts/bundle-qemu.sh

# 4. Notarize and package a distributable disk image (DIST_NAME and
#    DMG_VOLUME_NAME name the outputs)
./scripts/make-dmg.sh

# 5. Verify the exact signed/stapled artifact, including versions, Gatekeeper,
#    the bundled Tools CD and both Power Mac display cards
./scripts/verify-release.sh dist/ClassicMac.dmg 4.8.1 4.8.1
```

All scripts are idempotent and safe to re-run. Building needs the Xcode command line tools and [Homebrew](https://brew.sh).

The Platinum fonts and icons are generated from the MIT sources in
`platinum/` with `python3 platinum/tools/build_platinum_swift.py`; see
[`platinum/README.md`](platinum/README.md).

The guest-side binaries are committed, so a normal build needs no cross toolchain. If you change them, rebuild with:

```bash
# 68k qfb card firmware + driver (builds Retro68 into vendor/ on first run)
./scripts/build-qfb-rom.sh          # -> qfb/mac_qfb.rom

# PPC video driver (adds Retro68 PPC compilers + Universal Interfaces)
./scripts/build-ppcvid-ndrv.sh      # -> ppcvid/qemu_vga.ndrv

# Classicvirtio Quadra card firmware and Power Mac NDRV loader
./scripts/build-classicvirtio-floppy.sh  # -> shared/declrom + shared/ndrvloader

# Power Mac OpenBIOS firmware with the ATI Radeon node and the sound-objects
# fix (uses the OpenBIOS Linux builder container)
./scripts/build-openbios.sh               # -> screamer/openbios-ppc
```

## Repository layout

```
ClassicMac/
  VERSION                   # canonical release version shared by app, CMake and packagers
  AGENTS.md                 # contributor & AI agent workflow and doc-maintenance contract
  CLAUDE.md                 # concise agent project instructions
  app/                      # macOS SwiftUI app and PlatinumKit (SwiftPM package)
  native/                   # cross-platform C++17/Qt desktop app and Windows/Linux packagers
  desktop/                  # Windows/Linux QEMU build & runtime staging scripts and shared UI assets
  display/                  # -display classicmac: QEMU hands its screen to the app
  platinum/                 # MIT Platinum fonts, icons, Mac OS 9 icon provenance and generators
  ati/                      # emulated ATI Radeon R100: QEMU, macOS/Windows/Linux GL and OpenBIOS patches
  browser/                  # branded loopback-only noVNC display client (macOS optional browser mode)
  Resources/                # app icon, machine icon, Iconfactory artwork, Quadra ROM
  qfb/                      # nubus-qfb enhanced framebuffer device + 68k driver
    driver/                 #   68k card firmware + driver source (Retro68)
  ppcvid/                   # PPC live-resize: VGA host-resize + packed-depth patches
    driver/                 #   qemu_vga.ndrv source (QemuMacDrivers fork, Retro68)
  classicvirtio/            # removable floppy + Power Mac startup-driver + share cache patches
  powermac/                 # QEMU and OpenBIOS Mac OS 8/9 compatibility & boot fast-path patches
  virtio/                   # QEMU removable VirtIO block device patch
  screamer/                 # screamer (AWACS) PPC audio device + OpenBIOS + timing patches
  osx/                      # dedicated QEMU 10.0.13 engine, firmware and tests for PowerPC Mac OS X
  copland/                  # DingusPPC patches, silent audio backend and tests (macOS-only Copland)
  cpu/                      # PowerPC CPU/TCG fast-path patches (MMU contexts, FPU, branch cache)
  startup/                  # startup race fixes (Cuda shift interrupt, IDE SRST/PIO status drain, ATAPI)
  timers/                   # coherent VIA timers and Timer 2 calibration hold patches
  usb/                      # OHCI idle-frame batching and HID idle patches
  input/                    # relative game-mouse routing patch and resize/input tests
  monitor/                  # HMP/QMP media control bridge patch
  shared/                   # classicvirtio card firmware (declrom) + ndrvloader + PRAM seed
  cocoaui/                  # Cocoa display patches: menus, input helpers, media names
  audio/                    # CoreAudio backend patch
  ide/                      # PPC MacIO IDE/DBDMA timing fix + tracepoints
  guestcd/                  # Tools CD manifest + HFS copy tooling + ClassicMac Display guest extension
  ios/                      # iPad build patch, ExportOptions and TestFlight documentation
  docs/                     # architecture notes, User Manual (docs/manual/), screenshots and operations
  scripts/                  # build-qemu, bundle-qemu, build-guest-cd, make-dmg, notarize, verify-release...
```

> Mac OS installation media is **not** committed; download the version you want from the [WinWorld operating system library](https://winworldpc.com/library/operating-systems) (Mac OS 8.5, 8.6, 9.2.1, and the [9.2.2 Universal Install CD](https://macintoshgarden.org/apps/mac-os-922-universal) are supported) and import it through the app.

## How the interesting parts work

- **Optional browser display (macOS)** — when enabled for a machine, its run gets a loopback HTTP server and authenticated `/websockify` proxy bound only to `127.0.0.1`, bridged to QEMU's private Unix-domain VNC socket. The bundled noVNC client renders the framebuffer, sends keyboard and pointer input, scales it to the tab, reconnects across Power Mac restarts, and understands QEMU's relative-pointer extension for games. By default on macOS, Windows, and Linux, machines open directly in ClassicMac's Platinum guest window.
- **Quadra video** — `nubus-qfb`, a paravirtualized NuBus framebuffer (ported from [SolraBizna/qemu](https://github.com/SolraBizna/qemu) onto QEMU 11.0.2) with 68k card firmware and a driver built with Retro68.
- **Power Mac video** — QEMU's std VGA gains packed low-color and optimized framebuffer paths. The bundled `qemu_vga.ndrv` supplies the full classic Mac display-mode set without a guest installation.
- **ATI Radeon 3D** — `ati-vga,model=r100` (from RetroBoot) emulates the card Mac OS 9's ATI drivers expect, down to its command processor and 3D engine. 3D draws run on a separate thread through Apple's OpenGL 4.1 (loaded at run time, so QEMU never links OpenGL); finished frames return to the card's VRAM for display. OpenBIOS names the card `ATY,Rage6p` and programs its boot mode, and the emulated monitor's EDID offers exactly the screen size you chose.
- **Platinum interface** — `PlatinumKit` draws every control from measured Mac OS 8.1 pixel tables with a one-Mac-pixel-per-point painter, uses bitmap fonts compiled to pixel-square TrueType, and hosts SwiftUI content in borderless windows with their own pinstriped frames, WindowShade and modal dialog handling.
- **Restart and installer handoff** — classic Quadra and Power Mac models exit and relaunch cleanly at a reset boundary. This works around QEMU's broken in-place `mac99` reset and gives ClassicMac a safe point to recognize a newly blessed hard disk before the next boot.
- **Sound** — the ASC is patched to output silence when idle (`qfb/asc-silence.patch`); the Power Mac uses Mark Cave-Ayland's screamer device with a screamer-aware OpenBIOS build.
- **Power Mac storage** — MacIO IDE/DBDMA holds the final DMA descriptor for 1 ms before publishing completion. Real hardware cannot finish in the same scheduling window in which the driver starts an operation; without this small latency, cached host I/O can beat Mac OS 9's synchronous-wait setup and lose its wakeup, freezing Installer mid-copy. Focused MacIO/DBDMA tracepoints remain available for regression runs.
- **Mac OS 9 hard-disk startup** — hard-disk boots use a startup-only accounted instruction clock, zero-delay cached IDE completion, and PowerPC TCG fast paths so Mac OS 9's hardware and timebase polling does not sleep on the host. QEMU recognizes Finder's menu bar directly in guest VRAM—even headless and at arbitrary supported resolutions—then atomically returns the virtual clock and 25 MHz PowerPC timebase to real time before applications run. A 15-second app fallback covers unusual themes; installer-CD boots retain normal timing and the 1 ms IDE race safeguard. Six 512 MB cold boots of the 1920×1080 OS 9.2.2 test machine reached the automatic handoff in 9.92–10.22 seconds, including a reboot after verified clean shutdown, versus roughly 18.5 seconds on the former real-time-only path; exact results vary with host load and guest configuration.
- **Power Mac Mac OS 8 startup** — the machine uses a CUDA/G3 profile with an original-iMac OpenBIOS identity and classic Mac NVRAM/RTAS services. During CD startup, a read-only Virtio mirror lets the Mac OS 8.5/8.6 ROM read the selected disc before its IDE driver is active; the ordinary IDE CD stays present for Installer and Finder.
- **Quadra floppy storage** — a removable `virtio-blk-device` rides on the existing classicvirtio NuBus transport. A small extension to the 68k block driver adds writes, flushes, media-change events, and a host/guest eject handshake so Finder owns the unmount before QEMU removes the raw image.
- **Emulation speed** — all QEMU-backed machines run on QEMU's TCG JIT; an Apple Silicon Mac (and modern x64 Windows/Linux hosts) runs them comfortably faster than the original hardware.

## License

Original ClassicMac source code and ClassicMac-authored patches (including `powermac/openbios-classic-macos-8.patch` and the ClassicMac patches under `classicvirtio/`) are licensed under the [MIT License](LICENSE).

Patches and integrations targeting upstream GPL, LGPL, MPL, or Apache-licensed projects (such as QEMU, OpenBIOS, DingusPPC, UTM, noVNC, and `ppcvid/driver`) remain compatible with and subject to the respective upstream licenses when combined or distributed. Third-party components and separate artwork (such as `Resources/Iconfactory` and `platinum/macos-icons`) retain their own terms as documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
