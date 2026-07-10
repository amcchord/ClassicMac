# Changelog

## 1.3.0 — 2026-07-10

### Added

- **Borderless machine windows.** Choose View → Hide Title Bar or press
  Control-Option-T to remove the title, traffic-light controls, and titlebar
  separator for a clean guest-only window. The command becomes Show Title Bar
  while active and is also available from the machine's Dock menu.

### Fixed

- **Fullscreen now uses the complete drawable resolution.** The Cocoa display
  previously subtracted the screen's safe-area inset and could request a guest
  mode one titlebar-height shorter than the actual fullscreen content. It now
  reports the final content frame after the transition, opts the machine helper
  out of camera-housing compatibility mode, and leaves fullscreen sizing to
  AppKit.

## 1.2.1 — 2026-07-10

### Fixed

- **The Mac OS 9.2.1 install CD no longer stops at an Apple Audio Extension
  address error.** The failure requires a loaded second Tools CD and Sungem
  networking at the same time; either device alone boots normally. Networked
  Power Mac CD boots now leave the dedicated Tools tray empty at startup while
  keeping the drive available for manual insertion from the Machine menu after
  Finder appears.
- **Turning networking off now removes the emulated NIC.** QEMU creates each
  machine's default network adapter when no `-nic` option is supplied, so the
  old off path silently left networking enabled. ClassicMac now passes
  `-nic none` explicitly.
- **Power Mac tablet input now stays seamless while booting installer CDs.**
  The CD path used to ignore the enabled tablet setting, omit the classicvirtio
  driver loader, and fall back to a relative USB mouse that captured the host
  pointer. The loader installs the tablet NDRV and then resumes Open Firmware's
  selected boot device, so it can safely remain active while `-boot d` starts
  Mac OS 9.2.1 or 9.2.2 from CD.

## 1.2.0 — 2026-07-10

### Fixed

- **Mac OS 9.2.1 and 9.2.2 installations no longer freeze during file
  copy.** QEMU's MacIO IDE model could complete cached DBDMA I/O before
  classic Mac OS armed the synchronous wait for it, losing the wakeup and
  leaving Installer stuck forever around “About 4 minutes remaining.” The
  custom QEMU build now keeps the final DMA descriptor active for 1 ms before
  publishing IDE and DBDMA completion, matching the non-zero latency of real
  hardware. The same patch fixes a long-standing QEMU typo that sent ordinary
  hard-disk DMA reads through the ATAPI completion callback, latches the
  originating IDE unit for asynchronous completion, and adds focused trace
  events plus a headless install regression harness.

## 1.1.1 — 2026-07-09

### Fixed

- **Power Mac G4: tablet input no longer falls back to capturing the
  mouse.** QEMU treats whichever pointing device the guest touched last as
  "the mouse", and the `via=pmu` machine configuration includes a built-in
  USB mouse that steals pointer priority back from the virtio tablet as
  soon as Mac OS starts polling USB — so the window quietly returned to
  capture mode moments into every boot. With tablet input on, the Power
  Mac now runs as `via=pmu-adb` instead: the ADB mouse never re-asserts
  itself, so the tablet keeps priority once its driver loads, and ADB
  remains a working (captured) fallback for guests that can't run the
  classicvirtio driver, such as OS X or CD boots. With tablet input off,
  the machine keeps `via=pmu` and its USB mouse exactly as before.
- **No more "Press ⌃⌥G to release the mouse" window title in tablet
  mode.** With an absolute pointer nothing is captured, so the machine
  window now keeps its plain title while the mouse is inside it.

## 1.1 — 2026-07-05

### Added

- **Tablet input: the mouse moves seamlessly in and out of the machine
  window, no capture needed.** Both machines now present an absolute
  pointing device — classicvirtio's `virtio-tablet-device` on the Quadra
  800 and `virtio-tablet-pci` on the Power Mac G4 — so the host cursor maps
  directly onto the guest screen instead of being grabbed by the window.
  The bundled `declrom` and `ndrvloader` already carry the tablet driver,
  so nothing needs to be installed in the guest. Tablet input is on by
  default; a per-VM "Tablet input" toggle in the Hardware section falls
  back to traditional mouse capture if needed.

## 1.0.5 — 2026-07-04

### Added

- **Experimental macOS 15 (Sequoia) support.** The app no longer requires
  macOS 26 Tahoe: the Swift package targets macOS 15, the two Tahoe-only
  SwiftUI APIs are behind availability checks (the Start button falls back
  from Liquid Glass to `.borderedProminent`, and the toolbar spacer is
  skipped), `LSMinimumSystemVersion` is 15.0 for the app and helper bundles,
  and `build-qemu.sh` compiles QEMU with `MACOSX_DEPLOYMENT_TARGET=15.0`.
  Tahoe is unaffected — it keeps the same glass UI and the deployment target
  only lowers the minimum OS stamp. Support is experimental because the
  bundled Homebrew libraries (glib, pixman, libslirp, ...) are bottles built
  for the host OS; a bundle built on Tahoe is stamped for 15.0 but is only
  guaranteed Sequoia-clean when built on a macOS 15 machine.

## 1.0.4 — 2026-07-04

### Fixed

- **App icon out of "icon jail" on macOS 26 Tahoe.** macOS 26 draws any app
  that ships only a legacy `.icns` shrunken on a synthesized squircle
  backdrop, no matter how the icon artwork is shaped — 1.0.3's full-bleed
  `.icns` alone could not escape that. The app now also ships the icon in
  the Liquid Glass format: an Icon Composer document
  (`Resources/AppIcon.icon`, with the artwork extended to a full square
  layer) that `bundle-qemu.sh` compiles with `actool` into `Assets.car`,
  referenced from `CFBundleIconName`. macOS 26 renders that natively —
  single squircle, edge to edge — while older macOS keeps using the legacy
  `.icns`. Building the Liquid Glass icon needs Xcode 26; without it the
  bundle still builds and just falls back to the legacy icon.

## 1.0.3 — 2026-07-03

### Fixed

- **App icon no longer shrinks inside a grey border.** The checked-in
  `AppIcon.icns` had drifted to a version whose artwork only covered ~87% of
  the canvas; macOS renders undersized icon art on its own synthesized
  backdrop, producing a growing grey border around the icon with each
  regeneration. The original full-bleed master `AppIcon.png` is restored, the
  separate `.icns` copy is gone from the repo, and `bundle-qemu.sh` now
  generates the `.icns` fresh from the master PNG on every build so the two
  can never drift apart again.

## 1.0.2 — 2026-07-03

### Fixed

- **Power Mac drag-to-resize works again once a resolution has been saved in
  the guest.** Live window resizing on the Power Mac G4 stopped working as
  soon as a resolution was ever picked in the Monitors control panel: the
  Display Manager's re-probe (which the video driver triggers through a
  connect-change interrupt when the window is dragged) would revalidate the
  guest's *saved* display preference and stop, never adopting the
  window-sized mode. The bundled `qemu_vga.ndrv` now reports every other
  mode as invalid while a host window resize is pending, so the re-probe
  falls through to the driver's preferred configuration and the switch
  lands. Mode reporting returns to normal the moment the switch completes
  (or after ~10 seconds if no Display Manager is running), so the Monitors
  panel and boot-time resolution restore behave exactly as before. This
  also makes Resend Screen Resolution (Control-Option-R) reliable on the
  Power Mac.

## 1.0.1 — 2026-07-03

Quality-of-life update for the machine window: fullscreen is easy to leave and
the guest resolution can be re-requested on demand.

### Added

- **Fullscreen keyboard shortcut: Control-Option-F.** Toggles fullscreen from
  anywhere — including while the emulator has grabbed the keyboard and mouse,
  which is exactly what happens when a machine enters fullscreen. Previously
  the only shortcut was Command-F, which the guest swallowed in fullscreen,
  making it hard to get back out.
- **Resend Screen Resolution: Control-Option-R.** Pushes the current window
  size to the guest a second time, for the occasional case where the Mac
  misses the resolution-change request that accompanies a window resize (for
  example while the Display Manager is still starting up). Works on both the
  Quadra 800 and the Power Mac G4.
- **Both commands in the View menu** with their keyboard shortcuts shown, so
  they are easy to discover. The fullscreen item now reads "Enter Fullscreen"
  or "Exit Fullscreen" to match the window's current state.
- **Dock icon menu.** Right-click the running machine's Dock icon for
  Enter/Exit Fullscreen and Resend Screen Resolution — an always-reachable
  escape hatch even when the emulator window has captured all input.

### Changed

- The View menu's fullscreen shortcut changed from Command-F to
  Control-Option-F so that one combo works in every situation (Command
  shortcuts are delivered to the guest while input is grabbed).

## 1.0 — 2026-07-03

The first release of ClassicMac — the whole classic Mac OS era on Apple
Silicon.

- Emulates a **Macintosh Quadra 800** (68040) for System 7.1 – Mac OS 8.1 and
  a **Power Mac G4** (`mac99`) for Mac OS 8.5 – 9.2.2, on a custom QEMU 11.0.2
  build.
- Live window resizing on both machines: drag the window and the guest
  switches to that exact resolution.
- Enhanced Quadra framebuffer: any resolution up to 3840x2160, all QuickDraw
  depths including Thousands.
- Host folder sharing on both machines (classicvirtio + virtio-9p), with
  resource forks preserved.
- Working, clean sound: patched Apple Sound Chip (no idle buzz) and screamer
  (AWACS) on the Power Mac.
- Self-contained `.classic` machine documents — double-click to boot.
- Bundled guest-additions **Tools CD** (StuffIt Expander, Disk Copy, USB
  Overdrive, Transmit, ...), insertable at runtime.
