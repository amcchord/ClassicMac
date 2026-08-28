#!/usr/bin/env bash
# Build a small Apple Partition Map + HFS image containing GXMetal guest tools.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_DIR="$ROOT_DIR/gxmetal/guest/bin"
README_FILE="$ROOT_DIR/gxmetal/guest/README.txt"
OUTPUT_IMAGE="${1:-$ROOT_DIR/dist/GXMetal-Guest.iso}"
VERSION="$(sed -n 's/^#define GXMETAL_PRODUCT_VERSION_STRING "\([^"]*\)"/\1/p' \
    "$ROOT_DIR/gxmetal/guest/include/GXMetalVersion.h")"
[ -n "$VERSION" ] || { printf 'ERROR: cannot read GXMetal version\n' >&2; exit 1; }
VOLUME_NAME="GXMetal $VERSION"
IMAGE_BYTES=$((8 * 1024 * 1024))
HFS_START_BLOCK=64
MOUNTED=0

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

RETRO68_BIN="$ROOT_DIR/vendor/Retro68-build/toolchain/bin"
if [ -x "$RETRO68_BIN/hformat" ]; then
    export PATH="$RETRO68_BIN:$PATH"
fi
for tool in hformat hmount humount hcopy hattrib hmkdir hls; do
    command -v "$tool" >/dev/null 2>&1 || \
        die "hfsutils tool '$tool' is required (on macOS: brew install hfsutils)"
done
command -v python3 >/dev/null 2>&1 || die "python3 is required"

for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin GXMetalAGLProbe.bin \
                GXMetalEngineSelectionProbe.bin; do
    [ -f "$BIN_DIR/$artifact" ] || \
        die "$artifact is missing; run scripts/build-gxmetal.sh first"
done

OUTPUT_DIR="$(dirname "$OUTPUT_IMAGE")"
OUTPUT_NAME="$(basename "$OUTPUT_IMAGE")"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
OUTPUT_IMAGE="$OUTPUT_DIR/$OUTPUT_NAME"
WORK_IMAGE="$OUTPUT_IMAGE.tmp"

cleanup() {
    if [ "$MOUNTED" -eq 1 ]; then
        humount >/dev/null 2>&1 || true
    fi
    if [ -f "$WORK_IMAGE" ]; then
        rm -f -- "$WORK_IMAGE"
    fi
}
trap cleanup EXIT

rm -f -- "$WORK_IMAGE"
dd if=/dev/zero of="$WORK_IMAGE" bs=1024 \
    count=$((IMAGE_BYTES / 1024)) status=none

python3 - "$WORK_IMAGE" "$VOLUME_NAME" "$HFS_START_BLOCK" <<'PYEOF'
import struct
import sys

path, volume_name, hfs_start = sys.argv[1], sys.argv[2], int(sys.argv[3])
block_size = 512

with open(path, "rb") as source:
    data = bytearray(source.read())
total_blocks = len(data) // block_size

struct.pack_into(">H", data, 0, 0x4552)
struct.pack_into(">H", data, 2, block_size)
struct.pack_into(">I", data, 4, total_blocks)

def partition(index, map_count, physical_start, block_count, name, kind):
    offset = index * block_size
    struct.pack_into(">H", data, offset, 0x504D)
    struct.pack_into(">I", data, offset + 4, map_count)
    struct.pack_into(">I", data, offset + 8, physical_start)
    struct.pack_into(">I", data, offset + 12, block_count)
    data[offset + 16:offset + 16 + len(name)] = name.encode("ascii")
    data[offset + 48:offset + 48 + len(kind)] = kind.encode("ascii")
    struct.pack_into(">I", data, offset + 80, 0)
    struct.pack_into(">I", data, offset + 84, block_count)
    struct.pack_into(">I", data, offset + 88, 0x3B)

partition(1, 2, 1, hfs_start - 1, "Apple", "Apple_partition_map")
partition(2, 2, hfs_start, total_blocks - hfs_start,
          volume_name, "Apple_HFS")

with open(path, "wb") as destination:
    destination.write(data)
PYEOF

hformat -l "$VOLUME_NAME" "$WORK_IMAGE" 1 >/dev/null
hmount "$WORK_IMAGE" >/dev/null
MOUNTED=1

hcopy -t "$README_FILE" ":Read Me"
hattrib -t TEXT -c ttxt ":Read Me"
hcopy -m "$BIN_DIR/GXMetal.bin" ":GXMetal"
hcopy -m "$BIN_DIR/GXMetalInput.bin" ":GXMetal Input"
hcopy -m "$BIN_DIR/GXMetalStartup.bin" ":GXMetal Startup"
hcopy -m "$BIN_DIR/GXMetalInstaller.bin" ":Install GXMetal"
hcopy -m "$BIN_DIR/GXMetalTest.bin" ":GXMetal Test"
hcopy -m "$BIN_DIR/GXMetalAGLProbe.bin" ":GXMetal AGL Probe"
hcopy -m "$BIN_DIR/GXMetalEngineSelectionProbe.bin" \
    ":GXMetal RAVE Selection"

printf 'GXMetal guest image contents:\n'
hls -l
humount
MOUNTED=0
mv -f -- "$WORK_IMAGE" "$OUTPUT_IMAGE"
trap - EXIT

printf 'Built %s\n' "$OUTPUT_IMAGE"
