# Reliable ClassicMac Tools delivery in 1.8.1

## The 1.8.0 failure

ClassicMac 1.8.0 kept a dedicated IDE CD tray named `tools0` and changed its
medium when the user chose **Mac → Insert “ClassicMac Tools”**. QEMU reported
the HFS image as inserted, but Mac OS 9 did not re-enumerate that IDE tray or
ask Finder to mount the new volume. Restarting the guest recreated QEMU with
the saved Tools switch still off, so the empty tray remained empty.

## The 1.8.1 path

On a normal Power Mac hard-disk start, ClassicMac now exposes the same bundled
HFS image through a read-only `virtio-blk-pci` device. The existing
`ndrvloader` installs the PowerPC Virtio block NDRV before the Mac OS ROM boots,
just as it already does for shared folders, tablet input, and installer-media
support. The compatibility IDE tray remains empty, and the misleading live
Tools command is not shown on Power Macs.

The block NDRV posts a classic `diskEvt` when it discovers the volume. Finder
can start after that first event, so 1.8.1 keeps the driver's periodic callback
active and retries at a bounded rate until File Manager has created a volume
control block for the disk. It stops retrying immediately after the volume is
mounted.

Existing Power Mac configuration files are migrated once to turn on the new
startup volume. The migration writes a delivery-version marker the next time
the configuration is saved, so a user can subsequently turn Tools off and that
choice will remain off.

## Validation

- The Swift argument/migration suite verifies the empty IDE compatibility tray,
  read-only Virtio block graph, NDRV loader dependency, explicit-off behavior,
  installer-boot deferral, and one-time configuration migration.
- A disposable clone of the Mac OS 9.2 test disk was cold-started with the
  rebuilt NDRV loader and the exact bundled `ClassicMacTools.iso`.
- Finder mounted **ClassicMac Tools** automatically. The volume opened normally,
  and its **GXMetal** folder exposed **Install GXMetal**, **GXMetal Startup**,
  **GXMetal Test**, and the supplied puzzle-piece M icon.
- A second exact-bundle cold start enabled `virtio-9p-pci` simultaneously. OS 9
  mounted the writable **Shared Test** folder and read-only **ClassicMac Tools**
  side by side, confirming that Tools does not replace or interfere with the
  existing shared-directory feature.
- The source OS 9 disk was never attached to the validation process and was not
  modified.
- The exact Developer ID-signed 1.8.1 bundle then passed the complete in-guest
  GXMetal suite: RAVE discovery, depth, blending, alpha test, backface metadata,
  clipping, texturing, bitmap upload, dirty presentation, double buffering, and
  framebuffer checks. The matched workload measured 50,455 microseconds through
  GXMetal versus 725,457 microseconds through Apple Software RAVE, a **14.37x**
  accelerated-path speedup.
