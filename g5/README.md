# Power Mac G5 (`PowerMac7,3`) runtime

ClassicMac runs Power Mac G5 guests (`PowerMac7,3`, PowerPC 970FX) on an isolated QEMU 11.1.50 engine (`qemu-system-ppc64`) derived from Cat_7's `G5-openbios` branch together with OpenBIOS (`cat7/openbios` `G5-openbios`).

## Hardware profile

- **Machine**: `mac99,via=pmu` with `-cpu 970fx` (`PowerMac7,3`), U3 host bridge / AGP / HyperTransport / DART / MPIC, K2 I/O controller.
- **Processors & Memory**: 1–16 virtual CPUs (`-smp`, default 2; Mac OS X 10.3 uses at most 2 CPUs) and 512 MB–16 GB RAM (default 2048 MB; memory above 2 GB maps at 4 GB as on real G5 hardware).
- **Storage**: K2 SATA (`bus=sata.0`) for the startup hard disk (`disk.img`) and K2 ATA-100 (`if=ide`) for optical discs.
- **NVRAM**: Two 8 KB Core99 flash banks (`nvram.img`, 16 KB, auto-formatted from blank `0xFF` on first boot).
- **Audio & Networking**: K2 I2S with TAS3004 codec (`-global macio-newworld.audiodev=snd0`) and K2 GMAC (`sungem`).
- **Input**: K2 OHCI USB keyboard and mouse, plus optional `usb-tablet` for seamless pointer integration (and NEC uPD720101 USB 2.0 EHCI).
- **Graphics**:
  - **Standard** (`-vga std` + `qemu_vga.ndrv`, 64 MB VRAM, arbitrary preset or custom resolutions at 32-bit Millions of colors).
  - **ATI Radeon 9800** (`ati-radeon9800` on `pci.0,addr=0x10`, with host OpenGL/Metal acceleration via `gl=fast|on|off` and a user-supplied Mac card ROM).
  - **ATI Radeon 7000** (`ati-vga,model=rv100,agp=on` on `pci.0,addr=0x10`, with a user-supplied Mac card ROM).
- **Shared folder**: `virtio-9p-pci` in PCI-X slot (`bus=pcix1,addr=0x2`) with `macos_compat=on` (requires the `osx9p` guest extension inside Mac OS X).

## Pinned source & firmware

- **Engine**: `https://github.com/cat7/qemu.git` commit `8aae3e171ea1873104bd81bc438fe0c623c15780` (`G5-openbios` branch, QEMU `11.1.50`) with `g5/classicmac-display-g5.patch` (adds `-display classicmac` shared-memory Platinum window integration on macOS and Linux). Built by `scripts/build-g5-qemu.sh`.
- **OpenBIOS**: `https://github.com/cat7/openbios.git` commit `de698b8e0edc03800dfe454dc369cc4d67a97866` (`G5-openbios` branch), stored at `g5/firmware/openbios-ppc64-g5` (697,688 bytes, SHA-256 `109c8418fc4499c60b1500d1e5e7081cc61cfca05f7303e2347cdb6e105d0e6b`, built by `scripts/build-g5-openbios.sh`).
- **VGA driver & BIOS**:
  - `g5/firmware/qemu_vga.ndrv` (SHA-256 `9071b3ed97c9316aa2f70fa4b08eb69bf672aa0fc8ceb2ce2b421047bc82a409`)
  - `g5/firmware/vgabios-stdvga.bin` (SHA-256 `e8fc9e55790dbe3cb31f019a3deb57206ba6c54f5e581adb2ab2677a9d391472`)
