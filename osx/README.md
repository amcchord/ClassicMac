# PowerPC Mac OS X runtime

ClassicMac keeps the OS X machine on its own QEMU 10.0.13 engine and
Open Firmware. The profile uses the G4 7400, PMU, persistent Core99 flash,
native USB keyboard and mouse or tablet, ATA disks, Sungem, Screamer and
standard VGA with the RetroBoot widescreen driver. The default video
configuration supplies a software framebuffer. macOS presents it in a
Cocoa window or the optional browser view. Windows and Linux use the
existing native ClassicMac display.

Create a **Power Mac G4 — Mac OS X** and attach your PowerPC installation
disc image. In the installer, partition the disk with Apple Partition
Map and format as Mac OS Extended. If it restarts into the installer,
select the hard disk in ClassicMac's settings and start again. Apple
installation media is supplied by the user.

## Source and firmware

- Engine patch: [RetroBoot ead6d3b8](https://github.com/amcchord/RetroBoot/tree/ead6d3b89b8db21e263d1e19de6ffea8b0253200/emulator/osx),
  SHA-256 `46f87c4a24260c5d9684e5cd296ede3ad8ddf52109fdbbde4a7cc458a69fdadf`.
  ClassicMac applies it to upstream QEMU at
  `98f88efe8f07067667b8d9b1a5ce9551d34036c1` (v10.0.13). Native host
  display and audio are selected at build time. Windows supplies a stub
  for RetroBoot's optional Linux EGL loader.
- OpenBIOS: `e1e703ac0ad615ab0d7edea54db600aaa629bf07` from [OpenBIOS](https://github.com/openbios/openbios).
  Apply `powermac/openbios-classic-macos-8.patch` followed by the patches
  under `osx/openbios` in this order: airport-radio, sound-objects-terminator,
  pmu, native-find-wordlist, ati-radeon, macosx-profile, macosx-flash,
  macosx-numbers. The pinned builder and timestamp are specified in
  [RetroBoot's OpenBIOS source lock](https://github.com/amcchord/RetroBoot/blob/ead6d3b89b8db21e263d1e19de6ffea8b0253200/emulator/manifests/sources.lock.json).
  This copy comes from the published RetroBoot 2.0.2 product update and
  matches its source-built firmware hash
  `a942695593d47a82e20455dceaf91b2f6a03f881633a4cceef3d7f326f02b73d`.
- The GPL VGA driver is built from ClassicMac
  `3e245943fba0d18af875a8a3a50a148a6d865535` with
  `retroboot-osx-edid.patch`, using the existing Retro68 PowerPC toolchain.
  Its SHA-256 matches the upstream build lock
  `9071b3ed97c9316aa2f70fa4b08eb69bf672aa0fc8ceb2ce2b421047bc82a409`.
- `make_macosx_flash_seed.py` generates the 16 KiB flash template.
  Flash tests exercise the actual device, including erase/program,
  persistence, interrupted-bank recovery and invalid-bank rejection.
