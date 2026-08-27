#!/usr/bin/env bash
#
# Prepare an immutable Mac OS 9 game-sweep base from GXMetal guest artifacts
# bundled in a signed ClassicMac candidate, or from an explicitly supplied
# source-built Tools CD. The source image is never mounted or written; only a
# newly created clone is modified. QEMU and the Power Mac NDRV always come
# from the verified signed application.
#
# Usage:
#   scripts/prepare-gxmetal-game-base.sh SOURCE.img OUTPUT.img
#
# Optional environment:
#   GXMETAL_APP=/path/to/ClassicMac.app
#   GXMETAL_TOOLS_CD=/path/to/source-built/ClassicMacTools.iso
#   GXMETAL_NDRV=/path/to/source-built/qemu_vga.ndrv
#   GXMETAL_SKIP_DISK_CHECK_DISABLE=1
#   GXMETAL_FORCE_DISK_CHECK_VERIFY=1

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DISK="${1:-}"
OUTPUT_DISK="${2:-}"
APP="${GXMETAL_APP:-$ROOT_DIR/dist/ClassicMac.app}"
TOOLS_CD="${GXMETAL_TOOLS_CD:-$APP/Contents/Resources/ClassicMacTools.iso}"
PPC_QEMU="$APP/Contents/Helpers/Power Mac G4.app/Contents/MacOS/qemu-system-ppc"
PPC_NDRV="${GXMETAL_NDRV:-$APP/Contents/Resources/qemu/pc-bios/qemu_vga.ndrv}"

SCRATCH=""
ATTACH_DEVICE=""
HFS_PARTITION=""
MOUNT_POINT=""
HFSUTILS_MOUNTED=0
DISK_CHECK_ALREADY_DISABLED=0
DISK_CHECK_RESOURCE=""

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ "$HFSUTILS_MOUNTED" -eq 1 ]; then
    humount >/dev/null 2>&1 || true
  fi
  if [ -n "$HFS_PARTITION" ]; then
    diskutil unmount "$HFS_PARTITION" >/dev/null 2>&1 || true
  fi
  if [ -n "$ATTACH_DEVICE" ]; then
    diskutil eject "$ATTACH_DEVICE" >/dev/null 2>&1 || true
  fi
  if [ -n "$SCRATCH" ] && [ -d "$SCRATCH" ]; then
    rm -rf "$SCRATCH"
  fi
}
trap cleanup EXIT

for tool in cp diskutil ditto grep hcopy hmount humount mktemp plutil shasum \
            stat strings unar xcrun; do
  command -v "$tool" >/dev/null 2>&1 || die "Required tool not found: $tool"
done

[ -n "$SOURCE_DISK" ] || die "Pass a source Mac OS 9 disk image."
[ -n "$OUTPUT_DISK" ] || die "Pass a new output disk-image path."
[ -f "$SOURCE_DISK" ] || die "Source disk image not found: $SOURCE_DISK"
[ ! -e "$OUTPUT_DISK" ] || die "Output already exists: $OUTPUT_DISK"
[ -d "$APP" ] || die "ClassicMac application not found: $APP"
[ -f "$TOOLS_CD" ] || die "Bundled Tools CD not found: $TOOLS_CD"
[ -x "$PPC_QEMU" ] || die "Bundled Power Mac QEMU not found: $PPC_QEMU"
[ -f "$PPC_NDRV" ] || die "Bundled Power Mac NDRV not found: $PPC_NDRV"
CANDIDATE_APP_VERSION="$(plutil -extract CFBundleShortVersionString raw \
  "$APP/Contents/Info.plist")"
CANDIDATE_APP_ID="$(stat -f '%d:%i' "$APP")"
CANDIDATE_TOOLS_SHA="$(shasum -a 256 "$TOOLS_CD" | awk '{ print $1 }')"
CANDIDATE_QEMU_SHA="$(shasum -a 256 "$PPC_QEMU" | awk '{ print $1 }')"
CANDIDATE_NDRV_SHA="$(shasum -a 256 "$PPC_NDRV" | awk '{ print $1 }')"

SOURCE_DISK="$(cd "$(dirname "$SOURCE_DISK")" && pwd)/$(basename "$SOURCE_DISK")"
OUTPUT_PARENT="$(cd "$(dirname "$OUTPUT_DISK")" && pwd)"
OUTPUT_DISK="$OUTPUT_PARENT/$(basename "$OUTPUT_DISK")"
[ ! -w "$SOURCE_DISK" ] || \
  die "Source disk must be host-read-only (chmod a-w) before preparation: $SOURCE_DISK"
SOURCE_SIZE="$(stat -f '%z' "$SOURCE_DISK")"
SOURCE_MTIME="$(stat -f '%m' "$SOURCE_DISK")"

log "Verifying the signed application candidate"
if [ -n "${GXMETAL_TOOLS_CD:-}" ] || [ -n "${GXMETAL_NDRV:-}" ]; then
  # Source-built guest/NDRV candidates still use the exact signed QEMU app,
  # but intentionally differ from the repository's packaged artifacts.
  VERIFY_RELEASE_SKIP_REPO_FRESHNESS=1 \
    "$ROOT_DIR/scripts/verify-release.sh" "$APP"
else
  "$ROOT_DIR/scripts/verify-release.sh" "$APP"
fi

SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/gxmetal-game-base.XXXXXX")"
mkdir -p "$SCRATCH/macbinary" "$SCRATCH/guest"

if [ -n "${GXMETAL_TOOLS_CD:-}" ]; then
  log "Extracting GXMetal artifacts from the explicit Tools CD candidate"
else
  log "Extracting the packaged GXMetal artifacts"
fi
hmount "$TOOLS_CD" >/dev/null
HFSUTILS_MOUNTED=1
hcopy -m ':GXMetal:GXMetal' "$SCRATCH/macbinary/GXMetal.bin"
hcopy -m ':GXMetal:GXMetal Input' "$SCRATCH/macbinary/GXMetalInput.bin"
hcopy -m ':GXMetal:GXMetal Startup' "$SCRATCH/macbinary/GXMetalStartup.bin"
hcopy -m ':GXMetal:GXMetal Test' "$SCRATCH/macbinary/GXMetalTest.bin"
humount >/dev/null
HFSUTILS_MOUNTED=0

CANDIDATE_GXMETAL_VERSION="$(strings \
  "$SCRATCH/macbinary/GXMetal.bin" | \
  awk '/^[0-9]+\.[0-9]+([.][0-9]+| beta [0-9]+)?$/ { print; exit }')"
[ -n "$CANDIDATE_GXMETAL_VERSION" ] || \
  die "Packaged GXMetal driver does not contain a recognizable version."
for archive in "$SCRATCH"/macbinary/*.bin; do
  strings "$archive" | grep -Fx "$CANDIDATE_GXMETAL_VERSION" >/dev/null || \
    die "$(basename "$archive") does not report GXMetal version $CANDIDATE_GXMETAL_VERSION"
  unar -quiet -output-directory "$SCRATCH/guest" "$archive"
done

GXMETAL_FILE="$SCRATCH/guest/GXMetal"
INPUT_FILE="$SCRATCH/guest/GXMetal Input"
STARTUP_FILE="$SCRATCH/guest/GXMetal Startup"
TEST_FILE="$SCRATCH/guest/GXMetal Test"
[ -e "$INPUT_FILE" ] || INPUT_FILE="$SCRATCH/guest/GXMetalInput"
[ -e "$STARTUP_FILE" ] || STARTUP_FILE="$SCRATCH/guest/GXMetalStartup"
[ -e "$TEST_FILE" ] || TEST_FILE="$SCRATCH/guest/GXMetalTest"
for artifact in "$GXMETAL_FILE" "$INPUT_FILE" "$STARTUP_FILE" "$TEST_FILE"; do
  [ -e "$artifact" ] || die "Packaged artifact did not decode: $artifact"
done

log "Cloning the source image"
if ! cp -c "$SOURCE_DISK" "$OUTPUT_DISK" 2>/dev/null; then
  cp "$SOURCE_DISK" "$OUTPUT_DISK"
fi
chmod u+w "$OUTPUT_DISK"

log "Installing packaged GXMetal components into the clone"
ATTACH_OUTPUT="$(diskutil image attach --noMount "$OUTPUT_DISK")"
ATTACH_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | awk 'NR == 1 { print $1 }')"
HFS_PARTITION="$(printf '%s\n' "$ATTACH_OUTPUT" | \
  awk '$0 ~ /Apple_HFS/ { print $1; exit }')"
[ -n "$ATTACH_DEVICE" ] && [ -n "$HFS_PARTITION" ] || \
  die "The output image does not contain a mountable Apple_HFS partition."
diskutil mount "$HFS_PARTITION" >/dev/null
MOUNT_POINT="$(diskutil info -plist "$HFS_PARTITION" | \
  plutil -extract MountPoint raw -o - -)"
[ -d "$MOUNT_POINT/System Folder/Extensions" ] || \
  die "The output image does not contain a Mac OS System Folder."

BACKUP="$MOUNT_POINT/GXMetal Sweep Backups/Before $CANDIDATE_GXMETAL_VERSION-${CANDIDATE_TOOLS_SHA:0:12}"
TOOLS_FOLDER="$MOUNT_POINT/GXMetal Candidate Tools"
mkdir -p "$BACKUP" "$TOOLS_FOLDER"
for name in 'GXMetal' 'GXMetal Input' 'GXMetal Startup'; do
  if [ -e "$MOUNT_POINT/System Folder/Extensions/$name" ]; then
    mv "$MOUNT_POINT/System Folder/Extensions/$name" "$BACKUP/$name"
  fi
done
if [ -e "$TOOLS_FOLDER/GXMetal Test" ]; then
  mv "$TOOLS_FOLDER/GXMetal Test" "$BACKUP/GXMetal Test"
fi

ditto "$GXMETAL_FILE" "$MOUNT_POINT/System Folder/Extensions/GXMetal"
ditto "$INPUT_FILE" "$MOUNT_POINT/System Folder/Extensions/GXMetal Input"
ditto "$STARTUP_FILE" "$MOUNT_POINT/System Folder/Extensions/GXMetal Startup"
ditto "$TEST_FILE" "$TOOLS_FOLDER/GXMetal Test"

# A promoted game base must not inherit a conformance application as a Startup
# Item.  Its modal PASS/FAIL dialog can consume the first automated click and
# block every later semantic gate even though the game and driver are healthy.
# Preserve any inherited item in the candidate-qualified backup instead of
# deleting it.
for startup_test in 'GXMetal Test' 'GXMetal AGL Probe'; do
  if [ -e "$MOUNT_POINT/System Folder/Startup Items/$startup_test" ]; then
    mv "$MOUNT_POINT/System Folder/Startup Items/$startup_test" \
       "$BACKUP/$startup_test Startup Item"
  fi
done

# A promoted base starts with no result or trace inherited from earlier runs.
# Game evidence will contain the first diagnostic snapshot produced by this
# exact candidate rather than counters from the image used as its source.
for stale in 'GXMetal Test Results' 'GXMetal AGL Probe Results' \
             'GXMetal Driver Trace' 'GXMetal Input Trace'; do
  if [ -e "$MOUNT_POINT/System Folder/Preferences/$stale" ]; then
    mv "$MOUNT_POINT/System Folder/Preferences/$stale" "$BACKUP/$stale"
  fi
done

# Installing a driver and test application cannot change General Controls.
# Probe the persisted preference while the clone is already mounted so bases
# derived from a verified predecessor do not pay for two redundant VM boots.
if [ -f "$MOUNT_POINT/System Folder/Preferences/General Controls Prefs" ]; then
  DISK_CHECK_RESOURCE="$(xcrun DeRez -only Smrt \
     "$MOUNT_POINT/System Folder/Preferences/General Controls Prefs" \
     2>/dev/null || true)"
  if [ "$(printf '%s\n' "$DISK_CHECK_RESOURCE" | \
           grep -Ec "^data 'Smrt'")" = "1" ] && \
     printf '%s\n' "$DISK_CHECK_RESOURCE" | \
       grep -Eq "^data 'Smrt' \\(0\\) \\{$" && \
     printf '%s\n' "$DISK_CHECK_RESOURCE" | \
       grep -Eq '^[[:space:]]*\$"0000 0002"'; then
    DISK_CHECK_ALREADY_DISABLED=1
  fi
fi

sync
diskutil unmount "$HFS_PARTITION" >/dev/null
HFS_PARTITION=""
MOUNT_POINT=""
diskutil eject "$ATTACH_DEVICE" >/dev/null
ATTACH_DEVICE=""

if [ "${GXMETAL_SKIP_DISK_CHECK_DISABLE:-0}" != "1" ]; then
  if [ "$DISK_CHECK_ALREADY_DISABLED" -eq 1 ] && \
     [ "${GXMETAL_FORCE_DISK_CHECK_VERIFY:-0}" != "1" ]; then
    log "Mac OS 9 startup disk-check preference preserved from the verified base"
  else
    log "Persistently disabling Mac OS 9's improper-shutdown disk check"
    GXMETAL_APP="$APP" \
      "$ROOT_DIR/scripts/disable-os9-disk-check.sh" "$OUTPUT_DISK"
  fi
fi

[ "$(stat -f '%z' "$SOURCE_DISK")" = "$SOURCE_SIZE" ] && \
  [ "$(stat -f '%m' "$SOURCE_DISK")" = "$SOURCE_MTIME" ] || \
  die "The source disk's size or modification time changed."

# The bundle publisher promotes a signed app with an atomic rename. Detect a
# promotion (or an in-place artifact mutation) that overlaps this preparation
# so a base can never be labelled with hashes from a different candidate than
# the one whose guest components and QEMU were actually used.
[ "$(stat -f '%d:%i' "$APP")" = "$CANDIDATE_APP_ID" ] && \
  [ "$(shasum -a 256 "$TOOLS_CD" | awk '{ print $1 }')" = "$CANDIDATE_TOOLS_SHA" ] && \
  [ "$(shasum -a 256 "$PPC_QEMU" | awk '{ print $1 }')" = "$CANDIDATE_QEMU_SHA" ] && \
  [ "$(shasum -a 256 "$PPC_NDRV" | awk '{ print $1 }')" = "$CANDIDATE_NDRV_SHA" ] || \
  die "The signed ClassicMac candidate changed while preparing the base; discard the output and retry."

# The sweep creates and chmods a private per-run clone. Mark the promoted base
# read-only so an ad-hoc QEMU invocation cannot accidentally attach it writable.
chmod a-w "$OUTPUT_DISK"

log "Prepared immutable GXMetal game base"
printf '    App version:    %s\n' "$CANDIDATE_APP_VERSION"
printf '    GXMetal version: %s\n' "$CANDIDATE_GXMETAL_VERSION"
printf '    Source:         %s\n' "$SOURCE_DISK"
printf '    Source SHA-256: %s\n' "$(shasum -a 256 "$SOURCE_DISK" | awk '{ print $1 }')"
printf '    Output:         %s\n' "$OUTPUT_DISK"
printf '    Output SHA-256: %s\n' "$(shasum -a 256 "$OUTPUT_DISK" | awk '{ print $1 }')"
printf '    Tools CD SHA:   %s\n' "$CANDIDATE_TOOLS_SHA"
printf '    Power Mac SHA:  %s\n' "$CANDIDATE_QEMU_SHA"
printf '    Power NDRV SHA: %s\n' "$CANDIDATE_NDRV_SHA"
