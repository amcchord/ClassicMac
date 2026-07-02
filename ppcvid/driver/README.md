# ClassicMac Power Mac video driver (`qemu_vga.ndrv`)

A fork of the [QemuMacDrivers](https://github.com/qemu/QemuMacDrivers)
`QemuVGADriver` (GPL-2.0, see `COPYING`) - the native Mac OS video driver that
OpenBIOS hands to the guest for QEMU's std VGA device on the `mac99` machine.

## What the fork adds

Host-window-driven live resolution switching, mirroring the 68k `nubus-qfb`
driver in `qfb/driver`:

- Detects the **host-resize request channel** that
  `ppcvid/vga-host-resize.patch` adds to QEMU's std VGA (three read-only
  registers after the QEXT block in BAR2: requested width, height, and a
  serial that bumps on every change).
- Appends **two dynamic display modes** at stable IDs past the standard
  (EDID-derived) list. Their geometry is retargeted at runtime to whatever
  size the host requests; the pair alternates so the Display Manager always
  sees a real mode change.
- Polls the request serial from the driver's **pseudo-VBL timer**, debounces
  it (2 ticks) and then fires a **VSL connect-change interrupt**
  (`kFBConnectInterruptServiceType`). The driver also reports
  `kReportsHotPlugging`, so Mac OS re-probes the display - the same path used
  for real monitor hot-plugging - finds the retargeted mode advertised as the
  preferred/default configuration, and switches to it. Nothing needs to be
  installed inside the guest.
- A private status selector (`cscQemuVgaGetHostResize`) that a task-time
  guest agent could use to drive `DMSetDisplayMode` itself; unused today
  because the re-probe path works on its own, kept as a fallback hook.

The build (see `GNUmakefile`) uses the Retro68 `powerpc-apple-macos` compilers
with Apple's Universal Interfaces, linked into an ndrv PEF with the recipe
from [classicvirtio](https://github.com/elliotnunn/classicvirtio)
(`ndrv.lds`, MIT). Build it with `scripts/build-ppcvid-ndrv.sh`, which sets
all of that up.

## Behavior notes

- Widths snap down to a multiple of 8 (Bochs VBE constraint).
- Requests are clamped to 512x384 minimum (the QEMU window's minimum content
  size) and to what fits in VRAM at 32bpp.
- When the requested size exactly matches a standard mode, the standard mode
  is used so Monitors shows a familiar entry; the dynamic modes themselves
  are hidden from Monitors (`kModeShowNever`).
