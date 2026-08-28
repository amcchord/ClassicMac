#!/usr/bin/env bash
#
# Build a bootable Mac OS 9.2.1 CD that installs the ClassicMac additions as
# part of Apple's regular Easy Install / Custom Install flow.
#
# The source is cloned byte-for-byte, then only its HFS partition is edited.
# This preserves Apple's partition map, CD drivers, boot blocks, and blessing.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_IMAGE="${1:-${CLASSICMAC_OS9_SOURCE_ISO:-$ROOT_DIR/context/macos_921_ppc.iso}}"
OUT_IMAGE="${2:-${CLASSICMAC_OS9_OUTPUT:-$ROOT_DIR/dist/ClassicMac-Mac-OS-9.2.1.iso}}"
MANIFEST="$ROOT_DIR/guestcd/manifest.tsv"
DL_DIR="$ROOT_DIR/vendor/guest-cd/downloads"
HFS_COPY="$ROOT_DIR/guestcd/hfs-copy.py"
USB_EXTRACTOR="$ROOT_DIR/guestcd/extract-usb-overdrive.py"
PATCH_GENERATOR="$ROOT_DIR/guestcd/os9-installer/generate-installer-patch.py"
INSTALLER_TYPES="$ROOT_DIR/guestcd/os9-installer/InstallerTypes.r"
ADDITIONS_README="$ROOT_DIR/guestcd/os9-installer/Read Me.txt"
GXMETAL_BIN_DIR="$ROOT_DIR/gxmetal/guest/bin"
GXMETAL_README="$ROOT_DIR/gxmetal/guest/README.txt"
INSTALLER_HFS=":Software Installers:System Software:Mac OS 9.2.1:Install System Software"
PAYLOAD_HFS=":Software Installers:System Software:Mac OS 9.2.1:ClassicMac Additions"
VOLUME_NAME="Mac OS 9.2.1"
EXPECTED_SOURCE_SHA256="${CLASSICMAC_OS9_SOURCE_SHA256:-050c08fce1a82ee4ef656c50f7d1bd07c114abcf4b08cd4eff7d613fa84f006b}"
SYSTEM_REZ="/usr/bin/Rez"
SYSTEM_DEREZ="/usr/bin/DeRez"

WORK_DIR=""
WORK_IMAGE=""
MOUNTED=0

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ "$MOUNTED" -eq 1 ]; then
    humount >/dev/null 2>&1 || true
  fi
  if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
    rm -rf "$WORK_DIR"
  fi
  if [ -n "$WORK_IMAGE" ] && [ -f "$WORK_IMAGE" ]; then
    rm -f "$WORK_IMAGE"
  fi
}
trap cleanup EXIT

[ -f "$SOURCE_IMAGE" ] || die "Mac OS 9.2.1 source image not found: $SOURCE_IMAGE"
[ -f "$MANIFEST" ] || die "guest tools manifest not found: $MANIFEST"
[ -f "$HFS_COPY" ] || die "HFS copy helper not found: $HFS_COPY"

RETRO68_BIN="$ROOT_DIR/vendor/Retro68-build/toolchain/bin"
if [ -x "$RETRO68_BIN/hmount" ]; then
  export PATH="$RETRO68_BIN:$PATH"
fi

for tool in curl ditto file hattrib hcopy hdel hls hmkdir hmount humount hvol macbinary md5 python3 shasum unar xattr; do
  command -v "$tool" >/dev/null 2>&1 || die "required tool not found: $tool"
done
[ -x "$SYSTEM_REZ" ] || die "Apple Rez not found: $SYSTEM_REZ"
[ -x "$SYSTEM_DEREZ" ] || die "Apple DeRez not found: $SYSTEM_DEREZ"

md5_of() {
  md5 -q "$1"
}

archive_for() {
  local wanted="$1"
  local name handling expected_md5 url destination
  while IFS=$'\t' read -r name handling expected_md5 url; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ "$name" != "$wanted" ]; then
      continue
    fi
    destination="$DL_DIR/$(basename "$url")"
    if [ -f "$destination" ] && [ "$(md5_of "$destination")" = "$expected_md5" ]; then
      printf '    cached  %s\n' "$(basename "$destination")" >&2
    else
      printf '    fetch   %s\n' "$url" >&2
      curl -fsSL --retry 3 --max-time 300 -o "$destination.tmp" "$url" || die "download failed: $url"
      if [ "$(md5_of "$destination.tmp")" != "$expected_md5" ]; then
        rm -f "$destination.tmp"
        die "MD5 mismatch for $url"
      fi
      mv "$destination.tmp" "$destination"
    fi
    printf '%s\n' "$destination"
    return 0
  done < "$MANIFEST"
  die "manifest entry not found: $wanted"
}

decode_macbinary() {
  local source="$1" destination="$2"
  mkdir -p "$(dirname "$destination")"
  macbinary decode -n -o "$destination" "$source" >/dev/null
}

copy_metadata() {
  local source="$1" destination="$2"
  mkdir -p "$(dirname "$destination")"
  ditto --noqtn "$source" "$destination"
}

mark_simpletext() {
  local path="$1"
  # Finder type TEXT, creator ttxt, then 24 zero bytes.
  xattr -wx com.apple.FinderInfo 5445585474747874000000000000000000000000000000000000000000000000 "$path"
}

mkdir -p "$DL_DIR" "$(dirname "$OUT_IMAGE")"
WORK_DIR="$(mktemp -d)"
WORK_IMAGE="$OUT_IMAGE.tmp"
rm -f "$WORK_IMAGE"

log "Validating the source CD"
SOURCE_SHA256_BEFORE="$(shasum -a 256 "$SOURCE_IMAGE" | awk '{print $1}')"
if [ "$SOURCE_SHA256_BEFORE" != "$EXPECTED_SOURCE_SHA256" ]; then
  die "unsupported Mac OS 9.2.1 source SHA-256: $SOURCE_SHA256_BEFORE (expected $EXPECTED_SOURCE_SHA256)"
fi
SOURCE_DESCRIPTION="$(file "$SOURCE_IMAGE")"
case "$SOURCE_DESCRIPTION" in
  *"Apple Driver Map"*) ;;
  *) die "source is not an Apple-partitioned HFS CD image: $SOURCE_IMAGE" ;;
esac
python3 - "$SOURCE_IMAGE" <<'PYEOF'
import struct
import sys

path = sys.argv[1]
with open(path, "rb") as handle:
    handle.seek(512)
    first = handle.read(512)
    if first[:2] != b"PM":
        raise SystemExit("Apple partition map is missing")
    count = struct.unpack_from(">I", first, 4)[0]
    for index in range(1, count + 1):
        handle.seek(index * 512)
        entry = handle.read(512)
        if entry[48:80].split(b"\0", 1)[0] == b"Apple_HFS":
            break
    else:
        raise SystemExit("Apple_HFS partition is missing")
PYEOF

hmount "$SOURCE_IMAGE" >/dev/null
MOUNTED=1
hls "$INSTALLER_HFS" >/dev/null || die "the source does not contain the Mac OS 9.2.1 Installer"
SOURCE_VOLUME="$(LC_ALL=C hvol 2>/dev/null | head -6 | sed -n 's/^Volume name is "\(.*\)"$/\1/p' | head -1 || true)"
if [ -n "$SOURCE_VOLUME" ] && [ "$SOURCE_VOLUME" != "$VOLUME_NAME" ]; then
  die "expected source volume '$VOLUME_NAME', found '$SOURCE_VOLUME'"
fi
humount >/dev/null
MOUNTED=0

log "Building GXMetal"
"$ROOT_DIR/scripts/build-gxmetal.sh"

log "Fetching the four addition archives"
STUFFIT_ARCHIVE="$(archive_for 'StuffIt Expander 5.5.sea')"
DROPSTUFF_ARCHIVE="$(archive_for 'DropStuff 5.5 Installer')"
USB_ARCHIVE="$(archive_for 'USB Overdrive 1.4')"
TRANSMIT_ARCHIVE="$(archive_for 'Transmit 1.6 PPC')"

STAGE="$WORK_DIR/stage"
EXTENSIONS="$STAGE/System Extensions"
CONTROL_PANELS="$STAGE/Control Panels"
APPLE_EXTRAS="$STAGE/Apple Extras"
mkdir -p "$EXTENSIONS" "$CONTROL_PANELS" "$APPLE_EXTRAS"

log "Staging GXMetal"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetal.bin" "$EXTENSIONS/GXMetal"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalInput.bin" "$EXTENSIONS/GXMetal Input"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalStartup.bin" "$EXTENSIONS/GXMetal Startup"
mkdir -p "$APPLE_EXTRAS/GXMetal"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalInstaller.bin" "$APPLE_EXTRAS/GXMetal/Install GXMetal"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalTest.bin" "$APPLE_EXTRAS/GXMetal/GXMetal Test"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalAGLProbe.bin" "$APPLE_EXTRAS/GXMetal/GXMetal AGL Probe"
decode_macbinary "$GXMETAL_BIN_DIR/GXMetalEngineSelectionProbe.bin" "$APPLE_EXTRAS/GXMetal/GXMetal RAVE Selection"
copy_metadata "$GXMETAL_README" "$APPLE_EXTRAS/GXMetal/Read Me"
mark_simpletext "$APPLE_EXTRAS/GXMetal/Read Me"

log "Expanding USB Overdrive 1.4"
USB_OUTER="$WORK_DIR/usb-outer"
USB_MACBINARY="$WORK_DIR/usb-macbinary"
mkdir -p "$USB_OUTER" "$USB_MACBINARY" "$APPLE_EXTRAS/USB Overdrive"
unar -quiet -no-directory -output-directory "$USB_OUTER" "$USB_ARCHIVE"
USB_INSTALLER="$(find "$USB_OUTER" -type f -name 'USB Overdrive 1.4 Installer' -print -quit)"
[ -n "$USB_INSTALLER" ] || die "USB Overdrive installer was not found after expansion"
python3 "$USB_EXTRACTOR" "$USB_INSTALLER" "$USB_MACBINARY"
decode_macbinary "$USB_MACBINARY/USB Joystick Overdrive.bin" "$EXTENSIONS/USB Joystick Overdrive"
decode_macbinary "$USB_MACBINARY/USB Mouse Overdrive.bin" "$EXTENSIONS/USB Mouse Overdrive"
decode_macbinary "$USB_MACBINARY/USB Overdrive.bin" "$CONTROL_PANELS/USB Overdrive"
for name in "USB Overdrive Info" "USB Overdrive Serial"; do
  source_file="$(find "$USB_OUTER" -type f -name "$name" -print -quit)"
  if [ -n "$source_file" ]; then
    copy_metadata "$source_file" "$APPLE_EXTRAS/USB Overdrive/$name"
  fi
done

log "Expanding StuffIt Expander 5.5"
STUFFIT_SEA="$WORK_DIR/StuffIt Expander 5.5.sea"
STUFFIT_EXPANDED="$WORK_DIR/stuffit-expanded"
decode_macbinary "$STUFFIT_ARCHIVE" "$STUFFIT_SEA"
mkdir -p "$STUFFIT_EXPANDED" "$APPLE_EXTRAS/StuffIt 5.5"
unar -quiet -no-directory -output-directory "$STUFFIT_EXPANDED" "$STUFFIT_SEA"
STUFFIT_APP="$(find "$STUFFIT_EXPANDED" -maxdepth 2 -type f -name 'StuffIt Expander*' ! -name '*Temp*' -print -quit)"
[ -n "$STUFFIT_APP" ] || die "StuffIt Expander application was not found"
copy_metadata "$STUFFIT_APP" "$APPLE_EXTRAS/StuffIt 5.5/$(basename "$STUFFIT_APP")"
if [ -d "$STUFFIT_EXPANDED/Read Us First!" ]; then
  copy_metadata "$STUFFIT_EXPANDED/Read Us First!" "$APPLE_EXTRAS/StuffIt 5.5/Read Us First!"
fi

log "Expanding DropStuff 5.5 and its shared engine"
DROPSTUFF_OUTER="$WORK_DIR/dropstuff-outer"
DROPSTUFF_EXPANDED="$WORK_DIR/dropstuff-expanded"
mkdir -p "$DROPSTUFF_OUTER" "$DROPSTUFF_EXPANDED" "$APPLE_EXTRAS/DropStuff 5.5"
unar -quiet -no-directory -output-directory "$DROPSTUFF_OUTER" "$DROPSTUFF_ARCHIVE"
DROPSTUFF_INSTALLER="$(find "$DROPSTUFF_OUTER" -type f -name 'Aladdin DropStuff*Install' -print -quit)"
[ -n "$DROPSTUFF_INSTALLER" ] || die "DropStuff installer was not found after expansion"
unar -quiet -no-directory -output-directory "$DROPSTUFF_EXPANDED" "$DROPSTUFF_INSTALLER"
for name in "DropStuff™" "Aladdin Compression™" "PictoGuide™"; do
  if [ -e "$DROPSTUFF_EXPANDED/$name" ]; then
    copy_metadata "$DROPSTUFF_EXPANDED/$name" "$APPLE_EXTRAS/DropStuff 5.5/$name"
  fi
done
if [ -d "$DROPSTUFF_EXPANDED/Read Us First!" ]; then
  copy_metadata "$DROPSTUFF_EXPANDED/Read Us First!" "$APPLE_EXTRAS/DropStuff 5.5/Read Us First!"
fi
copy_metadata "$DROPSTUFF_EXPANDED/StuffIt Engine™" "$EXTENSIONS/StuffIt Engine"
copy_metadata "$DROPSTUFF_EXPANDED/StuffIt Engine™ PowerPlug" "$EXTENSIONS/StuffIt Engine PowerPlug"

log "Expanding Transmit 1.6 PPC"
TRANSMIT_EXPANDED="$WORK_DIR/transmit-expanded"
mkdir -p "$TRANSMIT_EXPANDED"
unar -quiet -no-directory -output-directory "$TRANSMIT_EXPANDED" "$TRANSMIT_ARCHIVE"
TRANSMIT_FOLDER="$(find "$TRANSMIT_EXPANDED" -maxdepth 1 -type d -name 'Transmit 1.6 PPC' -print -quit)"
[ -n "$TRANSMIT_FOLDER" ] || die "Transmit 1.6 PPC folder was not found"
copy_metadata "$TRANSMIT_FOLDER" "$APPLE_EXTRAS/Transmit 1.6 PPC"
if [ -n "${CLASSICMAC_TRANSMIT_SERIAL_FILE:-}" ]; then
  [ -f "$CLASSICMAC_TRANSMIT_SERIAL_FILE" ] || die "Transmit serial file not found: $CLASSICMAC_TRANSMIT_SERIAL_FILE"
  copy_metadata "$CLASSICMAC_TRANSMIT_SERIAL_FILE" "$APPLE_EXTRAS/Transmit 1.6 PPC/Transmit Registration"
  mark_simpletext "$APPLE_EXTRAS/Transmit 1.6 PPC/Transmit Registration"
fi

copy_metadata "$ADDITIONS_README" "$APPLE_EXTRAS/Read Me - ClassicMac Additions"
mark_simpletext "$APPLE_EXTRAS/Read Me - ClassicMac Additions"

PATCH_SOURCE="$WORK_DIR/ClassicMacInstallerPatch.r"
python3 "$PATCH_GENERATOR" "$STAGE" "$PATCH_SOURCE" --source-volume "$VOLUME_NAME"

log "Cloning the bootable source image"
cp "$SOURCE_IMAGE" "$WORK_IMAGE"

log "Adding payload files to the HFS partition"
hmount "$WORK_IMAGE" >/dev/null
MOUNTED=1
hmkdir "$PAYLOAD_HFS"
python3 "$HFS_COPY" "$EXTENSIONS" "$PAYLOAD_HFS:System Extensions"
python3 "$HFS_COPY" "$CONTROL_PANELS" "$PAYLOAD_HFS:Control Panels"
python3 "$HFS_COPY" "$APPLE_EXTRAS" "$PAYLOAD_HFS:Apple Extras"

log "Patching Apple's Installer package graph"
INSTALLER_MACBINARY="$WORK_DIR/Install System Software.bin"
INSTALLER_HOST="$WORK_DIR/Install System Software"
PATCHED_MACBINARY="$WORK_DIR/Install System Software.patched.bin"
hcopy -m "$INSTALLER_HFS" "$INSTALLER_MACBINARY"
macbinary decode -n -o "$INSTALLER_HOST" "$INSTALLER_MACBINARY" >/dev/null
"$SYSTEM_REZ" -a -ov -i "$(dirname "$INSTALLER_TYPES")" "$PATCH_SOURCE" -o "$INSTALLER_HOST"

VERIFY_REZ="$WORK_DIR/verify.r"
EXPECTED_ATOMS="$(grep -c "resource 'infa'" "$PATCH_SOURCE")"
"$SYSTEM_DEREZ" "$INSTALLER_HOST" "$INSTALLER_TYPES" -only inpk -only infa > "$VERIFY_REZ"
grep -q "'inpk', 24000" "$VERIFY_REZ" || die "Installer package 5000 does not reference the additions"
[ "$(grep -Ec "resource 'infa' \\(24[1-7][0-9][0-9]\\)" "$VERIFY_REZ")" -eq "$EXPECTED_ATOMS" ] || die "Installer does not contain every addition file atom"

VERIFY_ALL_REZ="$WORK_DIR/verify-all.r"
"$SYSTEM_DEREZ" "$INSTALLER_HOST" "$INSTALLER_TYPES" \
  -only infs -only intf -only infa > "$VERIFY_ALL_REZ"
[ "$(grep -c "resource 'infs'" "$VERIFY_ALL_REZ")" -ge "$EXPECTED_ATOMS" ] || \
  die "Installer is missing ClassicMac source-file resources"
[ "$(grep -c "resource 'intf'" "$VERIFY_ALL_REZ")" -ge "$EXPECTED_ATOMS" ] || \
  die "Installer is missing ClassicMac target-file resources"
[ "$(grep -Ec "resource 'infa' \\(24[1-7][0-9][0-9]\\)" "$VERIFY_ALL_REZ")" -eq "$EXPECTED_ATOMS" ] || \
  die "Installer atom verification count changed"

macbinary encode -t 2 -n -o "$PATCHED_MACBINARY" "$INSTALLER_HOST" >/dev/null
hattrib -l "$INSTALLER_HFS" >/dev/null 2>&1 || true
hdel "$INSTALLER_HFS"
hcopy -m "$PATCHED_MACBINARY" "$INSTALLER_HFS"

log "Verifying the modified HFS volume"
hls -l "$PAYLOAD_HFS"
hls "$INSTALLER_HFS" >/dev/null
humount >/dev/null
MOUNTED=0

log "Verifying that Apple's boot partitions are unchanged"
python3 - "$SOURCE_IMAGE" "$WORK_IMAGE" <<'PYEOF'
import os
import struct
import sys

source, output = sys.argv[1:]
block_size = 512

def hfs_start(path):
    with open(path, "rb") as handle:
        handle.seek(block_size)
        first = handle.read(block_size)
        if first[:2] != b"PM":
            raise SystemExit("Apple partition map is missing")
        count = struct.unpack_from(">I", first, 4)[0]
        for index in range(1, count + 1):
            handle.seek(index * block_size)
            entry = handle.read(block_size)
            ptype = entry[48:80].split(b"\0", 1)[0]
            if ptype == b"Apple_HFS":
                return struct.unpack_from(">I", entry, 8)[0]
    raise SystemExit("Apple_HFS partition is missing")

start = hfs_start(source)
if start != hfs_start(output):
    raise SystemExit("HFS partition start changed")
if os.path.getsize(source) != os.path.getsize(output):
    raise SystemExit("image size changed")

remaining = start * block_size
with open(source, "rb") as left, open(output, "rb") as right:
    while remaining:
        amount = min(1024 * 1024, remaining)
        if left.read(amount) != right.read(amount):
            raise SystemExit("a boot-driver or partition-map block changed")
        remaining -= amount
print(f"preserved {start} pre-HFS blocks ({start * block_size} bytes)")
PYEOF

[ "$(shasum -a 256 "$SOURCE_IMAGE" | awk '{print $1}')" = "$SOURCE_SHA256_BEFORE" ] || \
  die "source image changed during the build"

mv "$WORK_IMAGE" "$OUT_IMAGE"
WORK_IMAGE=""

log "Custom Mac OS 9.2.1 Installer CD complete"
printf '    image   %s\n' "$OUT_IMAGE"
printf '    SHA-256 %s\n' "$(shasum -a 256 "$OUT_IMAGE" | awk '{print $1}')"
