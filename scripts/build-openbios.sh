#!/usr/bin/env bash
#
# Rebuild the PowerPC OpenBIOS firmware bundled with ClassicMac.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
SOURCE_DIR="$VENDOR_DIR/openbios"
PATCH_FILE="$ROOT_DIR/powermac/openbios-classic-macos-8.patch"
OUTPUT_FILE="$ROOT_DIR/screamer/openbios-ppc"
OPENBIOS_REPO="${OPENBIOS_REPO:-https://github.com/openbios/openbios.git}"
OPENBIOS_COMMIT="${OPENBIOS_COMMIT:-e1e703ac0ad615ab0d7edea54db600aaa629bf07}"
BUILDER_IMAGE="${OPENBIOS_BUILDER_IMAGE:-ghcr.io/openbios/openbios-builder:master}"
BUILD_SOURCE=""

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ -n "$BUILD_SOURCE" ] && [ -d "$BUILD_SOURCE" ]; then
    git -C "$SOURCE_DIR" worktree remove --force "$BUILD_SOURCE" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

command -v docker >/dev/null 2>&1 || die "Docker is required to build OpenBIOS"

mkdir -p "$VENDOR_DIR"
if [ ! -d "$SOURCE_DIR/.git" ]; then
  log "Cloning OpenBIOS"
  git clone "$OPENBIOS_REPO" "$SOURCE_DIR"
fi

if ! git -C "$SOURCE_DIR" cat-file -e "$OPENBIOS_COMMIT^{commit}" 2>/dev/null; then
  log "Fetching pinned OpenBIOS revision"
  git -C "$SOURCE_DIR" fetch origin "$OPENBIOS_COMMIT"
fi

BUILD_SOURCE="$(mktemp -d "$VENDOR_DIR/openbios-build.XXXXXX")"
rmdir "$BUILD_SOURCE"
git -C "$SOURCE_DIR" worktree add --detach "$BUILD_SOURCE" "$OPENBIOS_COMMIT"

log "Applying Classic Mac OS 8 compatibility patch"
git -C "$BUILD_SOURCE" apply "$PATCH_FILE"

log "Building PowerPC OpenBIOS"
docker run --rm --platform linux/amd64 \
  -e "EXTRACFLAGS=-Wno-array-bounds -Wno-address-of-packed-member" \
  -v "$BUILD_SOURCE:/src" \
  -w /src \
  "$BUILDER_IMAGE" \
  sh -lc 'config/scripts/switch-arch ppc && make -j2'

FIRMWARE="$BUILD_SOURCE/obj-ppc/openbios-qemu.elf"
[ -f "$FIRMWARE" ] || die "OpenBIOS build did not produce $FIRMWARE"
if ! strings "$FIRMWARE" | grep "screamer" >/dev/null; then
  die "Built OpenBIOS is missing Screamer audio support"
fi
if ! strings "$FIRMWARE" | grep "nvram-fetch" >/dev/null; then
  die "Built OpenBIOS is missing classic Mac OS RTAS support"
fi

cp "$FIRMWARE" "$OUTPUT_FILE"
log "Updated $OUTPUT_FILE"
