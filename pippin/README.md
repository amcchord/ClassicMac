# Apple / Bandai Pippin Emulation (`-M pippin`)

ClassicMac integrates native **Apple / Bandai Pippin** support directly inside `qemu-system-ppc` (`-M pippin`, rebuilt from mainline QEMU v11.0.2 by `scripts/build-qemu.sh`) without requiring an external DingusPPC binary.

## Hardware Architecture

- **CPU:** PowerPC 603 @ 66 MHz (`CONFIG_PPC603` + `qemu-pippin-ppc603-rfi.patch`).
- **Memory & ROM:** Configurable 5–16 MiB RAM (default 13 MiB: 5 MiB base + 8 MiB expansion, passed as `-m 13`), paired with a 4 MiB Pippin Kinka boot ROM (`-bios <rom>`, e.g. Kinka 1.3 SHA-1 `8e512af6e34dd823f3defec77d43ecbff1ecad54`). ClassicMac does not bundle Pippin firmware, and optionally downloads & SHA-256-verifies Kinka 1.3 from Archive.org into `ROMs/pippin.rom` after explicit user acknowledgment of the Copyright & Software License Notice.
- **Bridge / IO Controller:** Aspen + GrandCentral MacIO controller (`hw_misc_macio_grandcentral.c`), CUDA VIA + 256 B PRAM (`qemu-pippin-cuda.patch`), Screamer/AWACS 44.1 kHz stereo audio (`audiodev=snd0`), and PCI/MMIO machine assembly (`hw_ppc_mac_pippin.c`).
- **Graphics (`taos`):** Apple Taos framebuffer & VBL interrupt controller (`hw_display_pippin_taos.c`) with NTSC (`640 × 480`), PAL (`640 × 512`), or VGA (`640 × 480`) output selected via `-M pippin,video-out=<ntsc|pal|vga>`, `DIRTY_MEMORY_VGA` scanline tracking, RGB555/8-bit LUT fast paths, and I2C clock/encoder hooks (`0x29` Athens, `0x44` Bt856, `0x45` TDA7433).
- **Storage & Persistence:**
  - `if=pflash,format=raw`: `flash.img` (128 KiB Save Flash, erased `0xFF`) for Pippin settings and game save files.
  - `if=mtd,index=0,format=raw`: `nvram.img` (8 KiB board NVRAM).
  - `if=mtd,index=1,format=raw`: `pram.img` (256 B CUDA PRAM).
- **CD-ROM & Apple SCSI Quirks:** MESH SCSI controller (`hw_scsi_mac_mesh.c`) + `scsi-cd` on `mesh-scsi.0,scsi-id=3` Sony CDU-8003A 1.9a profile (`qemu-pippin-scsi-cd.patch`) implementing Apple `READ_TOC` format decode, Features Page `0x31` (`.App`), `TYPE_ROM` `MODE_SELECT`, and `SET_CD_SPEED`.
- **Input:** `adb-applejack` (`hw_input_adb-applejack.c`, exposed at `/machine/applejack` with QOM property `buttons`) registered onto `ADBBusState` for relative trackball motion + 18-bit AppleJack controller button bitmasks (driven by `-display classicmac` `CMD_MSG_GAMEPAD = 23`, keyboard mappings, or `GCController` hardware gamepads) alongside `adb-keyboard`.

## ClassicMac Bundle Layout

A `.classic` bundle with `"machineFamily": "pippin"` omits `disk.img` and stores `flash.img`, `nvram.img`, and `pram.img` (plus optional bundle-local `pippin.rom` and converted `.iso` discs under `Discs/`). Raw 2352-byte Mode 1/2 `.bin`/`.cue` CD images are automatically stripped to standard 2048-byte `.iso` images by `PippinMachine.convertRawBinToISO`.
