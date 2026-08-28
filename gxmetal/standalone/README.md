# GXMetal 2.3.0 standalone release

This release separates GXMetal from the ClassicMac application. It provides a
ready-to-mount Mac OS guest image, the prebuilt PowerPC guest components, the
GXMetal-aware QEMU video NDRV, complete source, and a focused integration patch
for upstream QEMU 11.0.2.

GXMetal is a paravirtual QuickDraw 3D RAVE engine. It is not a driver that can
be added only inside Mac OS: the guest engine, QEMU command transport, host
renderer, and Power Mac video NDRV must be the matching versions supplied in
this release.

## Compatibility

- Host acceleration: macOS with Metal. Apple Silicon is the tested host. The
  source also compiles with the portable non-Metal stub, but that backend is a
  limited correctness rasterizer and deliberately does not advertise the full
  accelerated feature set.
- Guest: PowerPC Mac OS 9.2.1 and 9.2.2 are tested. Mac OS 8.5 and 8.6 remain
  possible but are not currently qualified.
- Emulator: the supplied patch applies cleanly to pristine QEMU 11.0.2. UTM
  uses a downstream QEMU fork, so its QEMU dependency may require a manual
  rebase and a rebuilt UTM application.
- Display: QEMU standard PCI VGA with its MMIO and QEMU extended registers
  enabled. The included `qemu_vga.ndrv` discovers GXMetal's BAR4 mapping and
  remains usable when GXMetal is disabled.

## Contents

- `GXMetal-Guest.iso`: Apple Partition Map + HFS image ready to attach as a
  read-only CD-ROM.
- `gxmetal/guest/bin/*.bin`: the same guest files in MacBinary form for tools
  that can preserve resource forks and Finder metadata.
- `gxmetal/standalone/qemu-v11.0.2.patch`: minimal upstream-QEMU integration,
  independent of ClassicMac's resize, packed-depth, cursor, and scanout patches.
- `ppcvid/qemu_vga.ndrv`: the matching Power Mac video NDRV.
- `scripts/apply-gxmetal-to-qemu.sh`: checked, repeatable patch-and-copy helper.
- `gxmetal/`: protocol, host, guest, QEMU, tests, and compatibility source.

## Upstream QEMU 11.0.2

Start with a clean QEMU 11.0.2 Git checkout and run:

```sh
scripts/apply-gxmetal-to-qemu.sh /path/to/qemu-11.0.2
```

On macOS, configure and build QEMU in the usual way. A minimal example is:

```sh
cd /path/to/qemu-11.0.2
./configure \
  --target-list=ppc-softmmu \
  --enable-cocoa \
  --enable-tcg \
  --disable-werror
ninja -C build qemu-system-ppc
build/qemu-system-ppc -device VGA,help | grep gxmetal
```

The final command must list the `gxmetal` property. The patch installs the
matching NDRV into `pc-bios`; re-run configure if the QEMU tree had already
been configured before applying GXMetal.

Enable the device on an existing standard VGA instance with these arguments:

```text
-global VGA.vgamem_mb=64
-global VGA.gxmetal=on
```

Using `-global` avoids creating a second VGA device and works with launchers
that already generate their own `-device VGA` or `-vga std` argument. To
disable GXMetal without changing the guest installation, change the second
value to `off`.

Attach `GXMetal-Guest.iso` as a read-only CD-ROM. In Mac OS 9, open the disc,
run **Install GXMetal**, restart, and run **GXMetal Test**. Proceed to games
only after the test reports PASS.

## UTM

An installed public UTM build does not contain this QEMU device, so adding only
the `VGA.gxmetal=on` argument is insufficient. Rebuild UTM with a patched QEMU:

1. Clone UTM recursively and obtain the QEMU source revision used by that UTM
   checkout.
2. Rebase `gxmetal/standalone/qemu-v11.0.2.patch` onto UTM's QEMU fork. For a
   fork still close to QEMU 11.0.2, this helper is a useful first attempt:

   ```sh
   GXMETAL_ALLOW_UNSUPPORTED_QEMU=1 \
     scripts/apply-gxmetal-to-qemu.sh /path/to/utm-qemu
   ```

3. Follow [UTM's macOS development instructions](https://github.com/utmapp/UTM/blob/main/Documentation/MacDevelopment.md).
   Its dependency builder accepts a custom QEMU source using
   `-q /path/to/utm-qemu`; rebuild the dependencies, UTM, and its signed
   package.
4. In the PowerPC VM's QEMU settings, add the two `-global` arguments shown
   above under [additional QEMU arguments](https://docs.getutm.app/settings-qemu/qemu/).
   UTM can export its resolved QEMU command if you need to confirm that it
   still uses standard VGA.
5. Add `GXMetal-Guest.iso` as a removable CD/DVD drive, then install and test
   from inside Mac OS 9.

The integration patch is intentionally small and keeps UTM-specific display
frontends out of the GXMetal device. Relative game input is implemented at
QEMU's common input layer, so Cocoa, VNC, and UTM's frontend can all respond to
the standard mouse-mode notification.

## Rebuilding the guest components

Prebuilt guest binaries are included; rebuilding them is optional. The
repository scripts build a Retro68 PowerPC/68K toolchain and extract the Apple
Universal Interfaces needed by the classic Mac driver and applications:

```sh
scripts/build-ppcvid-ndrv.sh
scripts/build-gxmetal.sh
scripts/build-gxmetal-guest-image.sh
```

Those bootstrap scripts currently target macOS and use Homebrew. If the
protocol header changes, rebuild both `qemu_vga.ndrv` and all guest components
before rebuilding the image and QEMU. Never combine guest files, NDRV, and host
sources from different protocol versions.

## Recovery and limitations

- If the guest no longer reaches Finder, start once with `VGA.gxmetal=off`,
  move **GXMetal**, **GXMetal Input**, and **GXMetal Startup** out of the System
  Folder's Extensions folder, and restart.
- Metal rendering is macOS-only in this release. Linux and Windows builds can
  compile the device but will normally fall back to Apple Software RAVE for
  contexts needing textures, depth, blending, or double buffering.
- iOS/iPadOS and saved-state/live-migration behavior have not been qualified.
- GXMetal is experimental. Keep a backup of the guest disk before installing
  any classic Mac OS extension.

See `gxmetal/COMPATIBILITY.md` for the audited RAVE capability contract and
`gxmetal/README.md` for protocol and implementation details.

## Licensing

GXMetal protocol, guest, tools, tests, and renderer files are distributed under
`gxmetal/LICENSE-MIT` unless a file says otherwise. The QEMU device glue carries
`GPL-2.0-or-later`. The derived Power Mac NDRV is GPL-2.0 and its license is in
`ppcvid/driver/COPYING`. QEMU itself is not included in this archive.
