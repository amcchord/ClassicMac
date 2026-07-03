# Changelog

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
