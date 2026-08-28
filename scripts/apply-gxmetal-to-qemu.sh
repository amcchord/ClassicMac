#!/usr/bin/env bash
# Apply the standalone GXMetal integration to an upstream QEMU 11.0.2 tree.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_FILE="$ROOT_DIR/gxmetal/standalone/qemu-v11.0.2.patch"
GXMETAL_DIR="$ROOT_DIR/gxmetal"
NDRV_FILE="$ROOT_DIR/ppcvid/qemu_vga.ndrv"
EXPECTED_QEMU_VERSION="11.0.2"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '==> %s\n' "$*"; }

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s /path/to/qemu-source\n' "$0" >&2
    exit 2
fi

QEMU_DIR="$(cd "$1" 2>/dev/null && pwd)" || \
    die "QEMU source directory does not exist: $1"

[ -e "$QEMU_DIR/.git" ] || \
    die "the target must be a Git checkout so the integration can be checked safely"
[ -f "$QEMU_DIR/hw/display/vga-pci.c" ] || \
    die "not a QEMU source tree: $QEMU_DIR"
[ -f "$PATCH_FILE" ] || die "standalone QEMU patch is missing: $PATCH_FILE"
[ -f "$NDRV_FILE" ] || die "Power Mac video NDRV is missing: $NDRV_FILE"

QEMU_VERSION="$(sed -n '1p' "$QEMU_DIR/VERSION" 2>/dev/null || true)"
if [ "$QEMU_VERSION" != "$EXPECTED_QEMU_VERSION" ]; then
    if [ -z "${GXMETAL_ALLOW_UNSUPPORTED_QEMU:-}" ]; then
        die "this patch targets QEMU $EXPECTED_QEMU_VERSION, but the tree reports ${QEMU_VERSION:-unknown}; set GXMETAL_ALLOW_UNSUPPORTED_QEMU=1 to try a downstream/UTM rebase"
    fi
    log "Trying the QEMU $EXPECTED_QEMU_VERSION patch against downstream version ${QEMU_VERSION:-unknown}"
fi

if git -C "$QEMU_DIR" apply --reverse --check "$PATCH_FILE" >/dev/null 2>&1; then
    log "GXMetal integration patch is already applied"
elif git -C "$QEMU_DIR" apply --check "$PATCH_FILE"; then
    log "Applying the standalone GXMetal QEMU integration"
    git -C "$QEMU_DIR" apply "$PATCH_FILE"
else
    die "the standalone patch does not apply cleanly; UTM/downstream QEMU users must rebase gxmetal/standalone/qemu-v11.0.2.patch onto their QEMU revision"
fi

log "Installing GXMetal transport and renderer sources"
for source_file in \
    "$GXMETAL_DIR/protocol/gxmetal_protocol.h" \
    "$GXMETAL_DIR/host/gxmetal_decode.h" \
    "$GXMETAL_DIR/host/gxmetal_decode.c" \
    "$GXMETAL_DIR/host/gxmetal_dirty.h" \
    "$GXMETAL_DIR/host/gxmetal_dirty.c" \
    "$GXMETAL_DIR/host/gxmetal_queue.h" \
    "$GXMETAL_DIR/host/gxmetal_queue.c" \
    "$GXMETAL_DIR/host/gxmetal_metal.h" \
    "$GXMETAL_DIR/host/gxmetal_metal.m" \
    "$GXMETAL_DIR/host/gxmetal_metal_stub.c" \
    "$GXMETAL_DIR/host/gxmetal_renderer.h" \
    "$GXMETAL_DIR/host/gxmetal_renderer.c" \
    "$GXMETAL_DIR/qemu/gxmetal_qemu.h" \
    "$GXMETAL_DIR/qemu/gxmetal_qemu.c"; do
    install -m 0644 "$source_file" \
        "$QEMU_DIR/hw/display/$(basename "$source_file")"
done

log "Installing the GXMetal-aware Power Mac video NDRV"
install -m 0644 "$NDRV_FILE" "$QEMU_DIR/pc-bios/qemu_vga.ndrv"

git -C "$QEMU_DIR" diff --check

printf '\nGXMetal is installed in %s. Reconfigure and rebuild qemu-system-ppc, then enable it with:\n' "$QEMU_DIR"
printf '    -global VGA.vgamem_mb=64 -global VGA.gxmetal=on\n'
printf 'Verify the binary with:\n'
printf '    build/qemu-system-ppc -device VGA,help | grep gxmetal\n'
