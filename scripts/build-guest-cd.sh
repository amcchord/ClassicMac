#!/usr/bin/env bash
#
# build-guest-cd.sh - Build the "ClassicMac Tools" guest additions CD image.
#
# Produces dist/ClassicMacTools.iso, a plain HFS (standard) volume that both
# System 7.1 (Quadra) and Mac OS 8/9 (Power Mac) mount natively when attached
# as a CD-ROM. The contents (StuffIt Expander, USB Overdrive, Disk Copy, ...)
# are downloaded from Macintosh Garden mirrors as listed in
# guestcd/manifest.tsv and written onto the volume with hfsutils so resource
# forks and type/creator codes survive (MacBinary is decoded; .sit archives
# are copied raw and expanded inside the guest).
#
# This script is idempotent: downloads are cached in vendor/guest-cd and only
# re-fetched when missing or failing their MD5 check; the image itself is
# rebuilt from scratch on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST="$ROOT_DIR/guestcd/manifest.tsv"
DL_DIR="$ROOT_DIR/vendor/guest-cd/downloads"
OUT_DIR="$ROOT_DIR/dist"
OUT_IMAGE="$OUT_DIR/ClassicMacTools.iso"
VOLUME_NAME="ClassicMac Tools"
# 16 MiB: plenty for ~5 MB of tools, and a multiple of the 2048-byte CD
# sector size.
IMAGE_BYTES=$((16 * 1024 * 1024))

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ -f "$MANIFEST" ] || die "manifest not found: $MANIFEST"

# ---------------------------------------------------------------------------
# 1. Locate hfsutils (Retro68 toolchain build preferred, then PATH)
# ---------------------------------------------------------------------------
RETRO68_BIN="$ROOT_DIR/vendor/Retro68-build/toolchain/bin"
if [ -x "$RETRO68_BIN/hformat" ]; then
  export PATH="$RETRO68_BIN:$PATH"
fi
for tool in hformat hmount humount hcopy hattrib hls; do
  command -v "$tool" >/dev/null 2>&1 || die "hfsutils tool '$tool' not found. Build the Retro68 toolchain (scripts/build-qfb-rom.sh) or 'brew install hfsutils'."
done

PYTHON_BIN="$(command -v python3 || true)"
[ -n "$PYTHON_BIN" ] || die "python3 is required to write the Apple Partition Map."

# ---------------------------------------------------------------------------
# 2. Download + verify the tools listed in the manifest
# ---------------------------------------------------------------------------
mkdir -p "$DL_DIR"

md5_of() {
  md5 -q "$1"
}

download_verified() {
  local url="$1" md5="$2" dest="$3"
  if [ -f "$dest" ] && [ "$(md5_of "$dest")" = "$md5" ]; then
    printf '    cached  %s\n' "$(basename "$dest")"
    return 0
  fi
  printf '    fetch   %s\n' "$url"
  curl -fsSL --retry 3 --max-time 300 -o "$dest.tmp" "$url" || die "download failed: $url"
  if [ "$(md5_of "$dest.tmp")" != "$md5" ]; then
    rm -f "$dest.tmp"
    die "MD5 mismatch for $url (expected $md5)"
  fi
  mv "$dest.tmp" "$dest"
}

log "Downloading guest tools (cached in vendor/guest-cd/downloads)"
while IFS=$'\t' read -r name handling md5 url; do
  case "$name" in ''|'#'*) continue ;; esac
  download_verified "$url" "$md5" "$DL_DIR/$(basename "$url")"
done < "$MANIFEST"

# ---------------------------------------------------------------------------
# 3. Generate the Read Me (SimpleText document)
# ---------------------------------------------------------------------------
README="$DL_DIR/ReadMe.txt"
cat > "$README" <<'EOF'
ClassicMac Tools CD
===================

A few essentials that make an emulated classic Mac much nicer to
use. Suggested install order:

1. StuffIt Expander 5.5  (68k + PPC, System 7.1.1 or later)
   Double-click "StuffIt Expander 5.5.sea"; it unpacks the
   installer. Run it. Install this first: the other items on this
   CD are .sit archives that Expander opens (drag them onto the
   Expander icon).

2. DropStuff 5.5 Installer.sit  (optional)
   Adds the Expander Enhancer, letting StuffIt Expander also open
   .zip and other formats. Expand, then run the installer.

3. Disk Copy 6.3.3.sit
   Apple's disk image utility; mounts .img and .smi images.
   Expand it, double-click the self-mounting image inside, and
   drag Disk Copy onto your hard disk.

4. Virtual CD-DVD Utility.sit
   Mounts .iso and .toast CD images straight from the Finder.
   Pairs well with the shared folder: drop a CD image into the
   share on your host Mac, then mount it here.

5. USB Overdrive 1.4.sit  (Power Mac only, Mac OS 8.5 - 9.2)
   Real scroll wheel and right-click support for the USB mouse.
   Expand, run the installer, restart. USB Overdrive is
   unregistered shareware, so it shows a reminder dialog now and
   then.

   IMPORTANT: after installing USB Overdrive, open this machine's
   settings in ClassicMac on your host Mac and turn OFF
   "Right-click & scroll wheel helpers", so clicks and scrolling
   are not doubled up.

On a Quadra (System 7 through Mac OS 8.1) only items 1-4 apply;
USB Overdrive needs a Power Mac running Mac OS 8.5 or later.
EOF

# ---------------------------------------------------------------------------
# 4. Build the HFS volume inside an Apple Partition Map
# ---------------------------------------------------------------------------
# The image is wrapped in a Driver Descriptor Record + Apple Partition Map so
# the 68k Quadra's ROM CD driver finds the HFS partition. Without the map, a
# bare HFS volume mounts on the Power Mac but the Quadra rejects it as
# "unreadable". hformat/hmount detect the HFS partition automatically.
log "Building $VOLUME_NAME image (Apple Partition Map + HFS)"
mkdir -p "$OUT_DIR"
WORK_IMAGE="$OUT_IMAGE.tmp"
rm -f "$WORK_IMAGE"
dd if=/dev/zero of="$WORK_IMAGE" bs=1024 count=$((IMAGE_BYTES / 1024)) status=none

# HFS partition starts at block 64 (after the DDR at 0 and the partition map),
# a conventional offset that leaves room for the map to grow.
HFS_START_BLOCK=64
"$PYTHON_BIN" - "$WORK_IMAGE" "$VOLUME_NAME" "$HFS_START_BLOCK" <<'PYEOF'
import struct
import sys

path, volume_name, hfs_start = sys.argv[1], sys.argv[2], int(sys.argv[3])
BS = 512

with open(path, "rb") as f:
    data = bytearray(f.read())
total_blocks = len(data) // BS

# Driver Descriptor Record (block 0)
struct.pack_into(">H", data, 0, 0x4552)         # sbSig 'ER'
struct.pack_into(">H", data, 2, BS)             # sbBlkSize
struct.pack_into(">I", data, 4, total_blocks)   # sbBlkCount

def partition(index, map_count, py_start, blk_count, name, ptype):
    off = index * BS
    struct.pack_into(">H", data, off + 0, 0x504D)      # pmSig 'PM'
    struct.pack_into(">I", data, off + 4, map_count)   # pmMapBlkCnt
    struct.pack_into(">I", data, off + 8, py_start)     # pmPyPartStart
    struct.pack_into(">I", data, off + 12, blk_count)  # pmPartBlkCnt
    data[off + 16:off + 16 + len(name)] = name.encode("ascii")
    data[off + 48:off + 48 + len(ptype)] = ptype.encode("ascii")
    struct.pack_into(">I", data, off + 80, 0)          # pmLgDataStart
    struct.pack_into(">I", data, off + 84, blk_count)  # pmDataCnt
    # valid | allocated | in use | readable | writable
    struct.pack_into(">I", data, off + 88, 0x3B)       # pmPartStatus

# Two-entry map: the self-descriptive map partition, then the HFS partition.
hfs_count = total_blocks - hfs_start
partition(1, 2, 1, hfs_start - 1, "Apple", "Apple_partition_map")
partition(2, 2, hfs_start, hfs_count, volume_name, "Apple_HFS")

with open(path, "wb") as f:
    f.write(data)
PYEOF

hformat -l "$VOLUME_NAME" "$WORK_IMAGE" 1 >/dev/null
hmount "$WORK_IMAGE" >/dev/null

# Read Me first so it lands at the top of the (unsorted) catalog.
hcopy -t "$README" ":Read Me"
hattrib -t TEXT -c ttxt ":Read Me"

while IFS=$'\t' read -r name handling md5 url; do
  case "$name" in ''|'#'*) continue ;; esac
  src="$DL_DIR/$(basename "$url")"
  case "$handling" in
    macbinary)
      # MacBinary: decode both forks + original type/creator.
      hcopy -m "$src" ":$name"
      ;;
    sit)
      # StuffIt archives are data-fork only; copy raw and tag them so
      # they show up with the right icon and open with Expander.
      hcopy -r "$src" ":$name"
      hattrib -t "SIT!" -c "SIT!" ":$name"
      ;;
    *)
      die "unknown handling '$handling' for $name"
      ;;
  esac
done < "$MANIFEST"

log "Volume contents:"
hls -l
humount

mv "$WORK_IMAGE" "$OUT_IMAGE"
log "Done. Built: $OUT_IMAGE"
log "Bundle it into the app with scripts/bundle-qemu.sh"
