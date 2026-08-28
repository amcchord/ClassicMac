#!/usr/bin/env bash
# Assemble the standalone GXMetal source, guest image, and QEMU integration.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION_HEADER="$ROOT_DIR/gxmetal/guest/include/GXMetalVersion.h"
VERSION="$(sed -n 's/^#define GXMETAL_PRODUCT_VERSION_STRING "\([^"]*\)"/\1/p' "$VERSION_HEADER")"
[ -n "$VERSION" ] || { printf 'ERROR: cannot read GXMetal version\n' >&2; exit 1; }

RELEASE_NAME="GXMetal-$VERSION"
STAGE_ROOT="$DIST_DIR/.$RELEASE_NAME.stage"
RELEASE_ROOT="$DIST_DIR/$RELEASE_NAME"
GUEST_IMAGE="$DIST_DIR/GXMetal-Guest.iso"
TAR_FILE="$DIST_DIR/$RELEASE_NAME.tar.gz"
ZIP_FILE="$DIST_DIR/$RELEASE_NAME.zip"
CHECKSUM_FILE="$DIST_DIR/$RELEASE_NAME-SHA256SUMS.txt"
INTERNAL_CHECKSUM_TMP="$DIST_DIR/.$RELEASE_NAME.internal-checksums.tmp"
BIN_DIR="$ROOT_DIR/gxmetal/guest/bin"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
log() { printf '\n==> %s\n' "$*"; }

case "$STAGE_ROOT" in
    "$DIST_DIR"/.GXMetal-*.stage) ;;
    *) die "refusing unsafe staging path: $STAGE_ROOT" ;;
esac
case "$RELEASE_ROOT" in
    "$DIST_DIR"/GXMetal-*) ;;
    *) die "refusing unsafe release path: $RELEASE_ROOT" ;;
esac

for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin GXMetalAGLProbe.bin \
                GXMetalEngineSelectionProbe.bin; do
    [ -f "$BIN_DIR/$artifact" ] || \
        die "$artifact is missing; run scripts/build-gxmetal.sh first"
    strings "$BIN_DIR/$artifact" | grep -F "$VERSION" >/dev/null || \
        die "$artifact does not report GXMetal version $VERSION"
done

PROTOCOL_SHA="$(shasum -a 256 "$ROOT_DIR/gxmetal/protocol/gxmetal_protocol.h" | awk '{print $1}')"
STAMP_SHA="$(sed -n '1p' "$ROOT_DIR/ppcvid/qemu_vga.ndrv.gxmetal-protocol.sha256")"
[ "$PROTOCOL_SHA" = "$STAMP_SHA" ] || \
    die "qemu_vga.ndrv is stale for the current GXMetal protocol"

log "Running native GXMetal tests"
make -C "$ROOT_DIR/gxmetal" test

log "Building the mountable guest image"
"$ROOT_DIR/scripts/build-gxmetal-guest-image.sh" "$GUEST_IMAGE"

log "Staging $RELEASE_NAME"
mkdir -p "$DIST_DIR"
if [ -e "$STAGE_ROOT" ]; then
    rm -rf -- "$STAGE_ROOT"
fi
mkdir -p "$STAGE_ROOT"

install -m 0644 "$ROOT_DIR/gxmetal/standalone/README.md" \
    "$STAGE_ROOT/README.md"
install -m 0644 "$ROOT_DIR/gxmetal/COMPATIBILITY.md" \
    "$STAGE_ROOT/COMPATIBILITY.md"
install -m 0644 "$GUEST_IMAGE" "$STAGE_ROOT/GXMetal-Guest.iso"

mkdir -p "$STAGE_ROOT/gxmetal" "$STAGE_ROOT/ppcvid" \
         "$STAGE_ROOT/qfb" "$STAGE_ROOT/scripts"
rsync -a \
    --exclude '.DS_Store' \
    --exclude 'build/' \
    --exclude 'guest/bin/' \
    "$ROOT_DIR/gxmetal/" "$STAGE_ROOT/gxmetal/"
rsync -a --exclude '.DS_Store' --exclude 'bin/' \
    "$ROOT_DIR/ppcvid/driver/" "$STAGE_ROOT/ppcvid/driver/"
rsync -a --exclude '.DS_Store' --exclude 'bin/' --exclude 'obj/' \
    "$ROOT_DIR/qfb/driver/" "$STAGE_ROOT/qfb/driver/"
install -m 0644 "$ROOT_DIR/ppcvid/qemu_vga.ndrv" \
    "$STAGE_ROOT/ppcvid/qemu_vga.ndrv"
install -m 0644 "$ROOT_DIR/ppcvid/qemu_vga.ndrv.gxmetal-protocol.sha256" \
    "$STAGE_ROOT/ppcvid/qemu_vga.ndrv.gxmetal-protocol.sha256"

mkdir -p "$STAGE_ROOT/gxmetal/guest/bin"
for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin GXMetalAGLProbe.bin \
                GXMetalEngineSelectionProbe.bin; do
    install -m 0644 "$BIN_DIR/$artifact" \
        "$STAGE_ROOT/gxmetal/guest/bin/$artifact"
done

for release_script in apply-gxmetal-to-qemu.sh build-gxmetal-guest-image.sh \
                      build-gxmetal.sh build-ppcvid-ndrv.sh \
                      build-qfb-rom.sh; do
    install -m 0755 "$ROOT_DIR/scripts/$release_script" \
        "$STAGE_ROOT/scripts/$release_script"
done

(
    cd "$STAGE_ROOT"
    find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | \
        while IFS= read -r release_file; do
            shasum -a 256 "$release_file"
        done > "$INTERNAL_CHECKSUM_TMP"
    mv "$INTERNAL_CHECKSUM_TMP" SHA256SUMS
)

if [ -e "$RELEASE_ROOT" ]; then
    rm -rf -- "$RELEASE_ROOT"
fi
mv "$STAGE_ROOT" "$RELEASE_ROOT"

log "Creating release archives"
rm -f -- "$TAR_FILE" "$ZIP_FILE" "$CHECKSUM_FILE"
COPYFILE_DISABLE=1 tar -C "$DIST_DIR" -czf "$TAR_FILE" "$RELEASE_NAME"
(
    cd "$DIST_DIR"
    COPYFILE_DISABLE=1 zip -X -q -r "$(basename "$ZIP_FILE")" "$RELEASE_NAME"
)
(
    cd "$DIST_DIR"
    shasum -a 256 "$(basename "$TAR_FILE")" \
                  "$(basename "$ZIP_FILE")" \
                  "$(basename "$GUEST_IMAGE")" > "$(basename "$CHECKSUM_FILE")"
)

printf '\nStandalone GXMetal release created:\n'
ls -lh "$TAR_FILE" "$ZIP_FILE" "$GUEST_IMAGE" "$CHECKSUM_FILE"
