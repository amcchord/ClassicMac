#!/usr/bin/env bash
#
# Rebuild the bundled 68k classicvirtio declaration ROM with ClassicMac's
# writable, removable floppy-image driver.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
SOURCE_DIR="$VENDOR_DIR/classicvirtio"
TOOLCHAIN="$VENDOR_DIR/Retro68-build/toolchain"
INTERFACES_DIR="$VENDOR_DIR/mpw/InterfacesAndLibraries"
PATCH_FILE="$ROOT_DIR/classicvirtio/floppy-driver.patch"
OUTPUT_FILE="$ROOT_DIR/shared/declrom"
CLASSICVIRTIO_REPO="${CLASSICVIRTIO_REPO:-https://github.com/elliotnunn/classicvirtio.git}"
CLASSICVIRTIO_COMMIT="${CLASSICVIRTIO_COMMIT:-fc401b4c731027cb4068f3415def7fe79f6659da}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

if [ ! -d "$SOURCE_DIR/.git" ]; then
  log "Cloning classicvirtio"
  git clone "$CLASSICVIRTIO_REPO" "$SOURCE_DIR"
fi

if ! git -C "$SOURCE_DIR" cat-file -e "$CLASSICVIRTIO_COMMIT^{commit}" 2>/dev/null; then
  log "Fetching pinned classicvirtio revision"
  git -C "$SOURCE_DIR" fetch origin "$CLASSICVIRTIO_COMMIT"
fi

log "Applying ClassicMac floppy driver patch"
git -C "$SOURCE_DIR" checkout "$CLASSICVIRTIO_COMMIT" -- device-block.c
git -C "$SOURCE_DIR" apply "$PATCH_FILE"

[ -x "$TOOLCHAIN/bin/m68k-apple-macos-gcc" ] ||
  die "Retro68 is not built. Run scripts/build-qfb-rom.sh first."

# The block driver uses Apple's disk-driver headers and 68k Interface glue.
# build-ppcvid-ndrv.sh extracts these from MPW into vendor/mpw; install them
# into the existing Retro68 toolchain when it still has Multiversal headers.
if [ ! -e "$TOOLCHAIN/m68k-apple-macos/include/Disks.h" ]; then
  [ -d "$INTERFACES_DIR" ] ||
    die "Universal Interfaces are missing. Run scripts/build-ppcvid-ndrv.sh first."
  log "Installing Universal Interfaces into Retro68"
  bash "$VENDOR_DIR/Retro68/interfaces-and-libraries.sh" \
    "$TOOLCHAIN" "$INTERFACES_DIR" true true false
fi

log "Building 68k classicvirtio declaration ROM"
PATH="$TOOLCHAIN/bin:$PATH" make -C "$SOURCE_DIR" build/classic/declrom

cp "$SOURCE_DIR/build/classic/declrom" "$OUTPUT_FILE"
log "Updated $OUTPUT_FILE"
