#!/usr/bin/env bash
#
# Rebuild the Power Mac G5 OpenBIOS firmware bundled with ClassicMac
# (g5/firmware/openbios-ppc64-g5).
#
# Source: cat7/openbios branch G5-openbios at commit
#   de698b8e0edc03800dfe454dc369cc4d67a97866
#
# Built with the pinned openbios-builder image (or native powerpc-linux-gnu-gcc
# when OPENBIOS_USE_NATIVE=1 on Linux).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
SOURCE_DIR="$VENDOR_DIR/openbios-g5"
OUTPUT_FILE="${OPENBIOS_OUTPUT:-$ROOT_DIR/g5/firmware/openbios-ppc64-g5}"
OPENBIOS_REPO="${OPENBIOS_REPO:-https://github.com/cat7/openbios.git}"
OPENBIOS_COMMIT="${OPENBIOS_COMMIT:-de698b8e0edc03800dfe454dc369cc4d67a97866}"
BUILDER_IMAGE="${OPENBIOS_BUILDER_IMAGE:-ghcr.io/openbios/openbios-builder@sha256:bf806644ccff091323dd56a853866ba09301b1a7fcb91e865fdb289499ef8b38}"
BUILD_DATE="${OPENBIOS_BUILD_DATE:-Oct 17 2026 00:00}"
EXTRACFLAGS="-Wno-array-bounds -Wno-address-of-packed-member"
MAKE_JOBS="${OPENBIOS_MAKE_JOBS:-4}"
BUILD_SOURCE=""
SHIM_DIR=""

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ -n "$BUILD_SOURCE" ] && [ -d "$BUILD_SOURCE" ]; then
    git -C "$SOURCE_DIR" worktree remove --force "$BUILD_SOURCE" >/dev/null 2>&1 || true
  fi
  if [ -n "$SHIM_DIR" ] && [ -d "$SHIM_DIR" ]; then
    rm -rf "$SHIM_DIR"
  fi
}
trap cleanup EXIT

mkdir -p "$VENDOR_DIR"
if [ ! -d "$SOURCE_DIR/.git" ]; then
  log "Cloning cat7/openbios"
  git clone --branch G5-openbios "$OPENBIOS_REPO" "$SOURCE_DIR"
fi

if ! git -C "$SOURCE_DIR" cat-file -e "$OPENBIOS_COMMIT^{commit}" 2>/dev/null; then
  log "Fetching pinned G5 OpenBIOS revision"
  git -C "$SOURCE_DIR" fetch origin "$OPENBIOS_COMMIT"
fi

SHIM_DIR="$(mktemp -d "$VENDOR_DIR/openbios-g5-shim.XXXXXX")"
cat > "$SHIM_DIR/date" <<'EOF'
#!/bin/sh
if [ "$*" = "+%b %e %Y %H:%M" ]; then echo "${CLASSICMAC_OPENBIOS_DATE:?}"; exit 0; fi
exec /bin/date "$@"
EOF
chmod 755 "$SHIM_DIR/date"

BUILD_SOURCE="$(mktemp -d "$VENDOR_DIR/openbios-g5-build.XXXXXX")"
rmdir "$BUILD_SOURCE"
git -C "$SOURCE_DIR" worktree add --detach "$BUILD_SOURCE" "$OPENBIOS_COMMIT" >/dev/null

log "Building G5 OpenBIOS (banner date: $BUILD_DATE)"
if [ "${OPENBIOS_USE_NATIVE:-0}" = "1" ]; then
  PATH="$SHIM_DIR:$PATH" \
  CLASSICMAC_OPENBIOS_DATE="$BUILD_DATE" \
  EXTRACFLAGS="$EXTRACFLAGS" \
  CROSS_COMPILE="${CROSS_COMPILE:-powerpc-linux-gnu-}" \
  make -C "$BUILD_SOURCE" distclean >/dev/null 2>&1 || true
  (
    cd "$BUILD_SOURCE"
    PATH="$SHIM_DIR:$PATH" CLASSICMAC_OPENBIOS_DATE="$BUILD_DATE" \
      CROSS_COMPILE="${CROSS_COMPILE:-powerpc-linux-gnu-}" \
      ./config/scripts/switch-arch ppc >/dev/null
    PATH="$SHIM_DIR:$PATH" CLASSICMAC_OPENBIOS_DATE="$BUILD_DATE" \
      CROSS_COMPILE="${CROSS_COMPILE:-powerpc-linux-gnu-}" \
      EXTRACFLAGS="$EXTRACFLAGS" \
      make -j"$MAKE_JOBS"
  )
else
  command -v docker >/dev/null 2>&1 || die "Docker (or OPENBIOS_USE_NATIVE=1 with powerpc-linux-gnu-gcc) is required"
  docker run --rm --platform linux/amd64 \
    -e "EXTRACFLAGS=$EXTRACFLAGS" \
    -e "CLASSICMAC_OPENBIOS_DATE=$BUILD_DATE" \
    -e "MAKE_JOBS=$MAKE_JOBS" \
    -e "PATH=/classicmac-shim:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    -v "$SHIM_DIR:/classicmac-shim:ro" \
    -v "$BUILD_SOURCE:/src" \
    -w /src \
    "$BUILDER_IMAGE" \
    sh -c 'config/scripts/switch-arch ppc >/dev/null && make -j"$MAKE_JOBS"'
fi

FIRMWARE="$BUILD_SOURCE/obj-ppc/openbios-qemu.elf"
[ -f "$FIRMWARE" ] || die "OpenBIOS build did not produce obj-ppc/openbios-qemu.elf"
strings "$FIRMWARE" | grep -q "PowerMac7,3" || die "Built OpenBIOS is missing PowerMac7,3 node"

mkdir -p "$(dirname "$OUTPUT_FILE")"
cp "$FIRMWARE" "$OUTPUT_FILE"
log "Updated $OUTPUT_FILE"
printf '    OpenBIOS commit:      %s\n' "$OPENBIOS_COMMIT"
printf '    openbios-ppc64-g5:    %s\n' "$(shasum -a 256 "$OUTPUT_FILE" | awk '{ print $1 }')"
