#!/usr/bin/env bash
#
# Extract machine-readable GXMetal results from a retained game-sweep disk.
# The image is attached read-only and is never modified.
#
# Usage:
#   scripts/extract-gxmetal-guest-results.sh agl RUN_DIRECTORY
#   scripts/extract-gxmetal-guest-results.sh test RUN_DIRECTORY
#   scripts/extract-gxmetal-guest-results.sh trace RUN_DIRECTORY
#   scripts/extract-gxmetal-guest-results.sh input RUN_DIRECTORY
#   scripts/extract-gxmetal-guest-results.sh selection RUN_DIRECTORY

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KIND="${1:-}"
RUN_DIR="${2:-}"
ATTACH_DEVICE=""
HFS_PARTITION=""

die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ -n "$HFS_PARTITION" ]; then
    diskutil unmount "$HFS_PARTITION" >/dev/null 2>&1 || true
  fi
  if [ -n "$ATTACH_DEVICE" ]; then
    diskutil eject "$ATTACH_DEVICE" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

case "$KIND" in
  agl|test|trace|input|selection) ;;
  *) die "First argument must be 'agl', 'test', 'trace', 'input', or 'selection'." ;;
esac
[ -n "$RUN_DIR" ] || die "Pass a retained game-sweep run directory."
[ -d "$RUN_DIR" ] || die "Run directory not found: $RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd)"
DISK_IMAGE="$RUN_DIR/disk.img"
[ -f "$DISK_IMAGE" ] || die "Retained disk not found: $DISK_IMAGE"

for tool in awk cp diskutil plutil python3 shasum; do
  command -v "$tool" >/dev/null 2>&1 || die "Required tool not found: $tool"
done

ATTACH_OUTPUT="$(diskutil image attach -readonly --noMount "$DISK_IMAGE")"
ATTACH_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | \
  awk 'NR == 1 { print $1 }')"
HFS_PARTITION="$(printf '%s\n' "$ATTACH_OUTPUT" | \
  awk '$0 ~ /Apple_HFS/ { print $1; exit }')"
[ -n "$ATTACH_DEVICE" ] && [ -n "$HFS_PARTITION" ] || \
  die "The retained image has no mountable Apple_HFS partition."

diskutil mount readOnly "$HFS_PARTITION" >/dev/null
MOUNT_POINT="$(diskutil info -plist "$HFS_PARTITION" | \
  plutil -extract MountPoint raw -o - -)"
PREFERENCES="$MOUNT_POINT/System Folder/Preferences"
[ -d "$PREFERENCES" ] || die "Mac OS Preferences folder not found."

if [ "$KIND" = "agl" ]; then
  SOURCE_RESULT="$PREFERENCES/GXMetal AGL Probe Results"
  RESULT_FILE="$RUN_DIR/agl-probe-results.txt"
elif [ "$KIND" = "test" ]; then
  SOURCE_RESULT="$PREFERENCES/GXMetal Test Results"
  RESULT_FILE="$RUN_DIR/gxmetal-test-results.txt"
elif [ "$KIND" = "selection" ]; then
  SOURCE_RESULT="$PREFERENCES/GXMetal RAVE Selection Results"
  RESULT_FILE="$RUN_DIR/engine-selection-results.json"
else
  SOURCE_RESULT=""
  RESULT_FILE=""
fi
TRACE_SOURCE="$PREFERENCES/GXMetal Driver Trace"
INPUT_TRACE_SOURCE="$PREFERENCES/GXMetal Input Trace"
if [ -n "$SOURCE_RESULT" ]; then
  [ -f "$SOURCE_RESULT" ] || die "Expected $KIND result was not written."
fi
if [ "$KIND" = "input" ]; then
  [ -f "$INPUT_TRACE_SOURCE" ] || die "GXMetal Input did not write a trace."
else
  [ -f "$TRACE_SOURCE" ] || die "GXMetal did not write a driver trace."
fi

if [ -n "$SOURCE_RESULT" ]; then
  cp "$SOURCE_RESULT" "$RESULT_FILE"
fi
if [ "$KIND" = "input" ]; then
  cp "$INPUT_TRACE_SOURCE" "$RUN_DIR/input-trace.bin"
  python3 "$ROOT_DIR/scripts/decode-gxmetal-input-trace.py" \
    "$RUN_DIR/input-trace.bin" > "$RUN_DIR/input-trace.json"
else
  cp "$TRACE_SOURCE" "$RUN_DIR/driver-trace.bin"
  python3 "$ROOT_DIR/scripts/decode-gxmetal-diagnostics.py" \
    "$RUN_DIR/driver-trace.bin" > "$RUN_DIR/driver-trace.json"
fi

printf 'Extracted %s evidence from %s\n' "$KIND" "$DISK_IMAGE"
if [ -n "$RESULT_FILE" ]; then
  printf '    Result: %s\n' "$RESULT_FILE"
fi
if [ "$KIND" = "input" ]; then
  printf '    Trace:  %s\n' "$RUN_DIR/input-trace.json"
else
  printf '    Trace:  %s\n' "$RUN_DIR/driver-trace.json"
fi
printf '    Disk SHA-256: %s\n' \
  "$(shasum -a 256 "$DISK_IMAGE" | awk '{ print $1 }')"
