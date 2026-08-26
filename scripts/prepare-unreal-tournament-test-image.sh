#!/usr/bin/env bash
# Create a copy-on-write game image whose classic Unreal Tournament audio
# startup is disabled. The Mac port can hang before its first RAVE submission
# when UseSound=True under the current emulated sound path; fullscreen is not
# involved. Only the cloned output is ever attached writable.

set -euo pipefail

usage() {
    echo "usage: $0 SOURCE.img OUTPUT.img" >&2
    exit 2
}

[ "$#" -eq 2 ] || usage

SOURCE_IMAGE="$1"
OUTPUT_IMAGE="$2"
GXMETAL_TMP_ROOT="${TMPDIR:-/tmp}"
WORK_DIR=""
WHOLE_DEVICE=""
HFS_DEVICE=""
MOUNTED=0

[ -f "$SOURCE_IMAGE" ] || {
    echo "source image not found: $SOURCE_IMAGE" >&2
    exit 1
}
[ ! -e "$OUTPUT_IMAGE" ] || {
    echo "refusing to overwrite output: $OUTPUT_IMAGE" >&2
    exit 1
}

for tool in cp diskutil hdiutil mktemp python3 shasum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "required tool not found: $tool" >&2
        exit 1
    }
done

cleanup() {
    if [ "$MOUNTED" -eq 1 ] && [ -n "$HFS_DEVICE" ]; then
        diskutil unmount "$HFS_DEVICE" >/dev/null 2>&1 || true
    fi
    if [ -n "$WHOLE_DEVICE" ]; then
        hdiutil detach "$WHOLE_DEVICE" >/dev/null 2>&1 || true
    fi
    if [ -n "$WORK_DIR" ] && [ -d "$WORK_DIR" ]; then
        rmdir "$WORK_DIR" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

SOURCE_SHA="$(shasum -a 256 "$SOURCE_IMAGE" | awk '{print $1}')"
cp -c "$SOURCE_IMAGE" "$OUTPUT_IMAGE"

ATTACH_OUTPUT="$(hdiutil attach -nomount \
    -imagekey diskimage-class=CRawDiskImage "$OUTPUT_IMAGE")"
WHOLE_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | awk 'NR == 1 {print $1}')"
HFS_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | \
    awk '$2 == "Apple_HFS" {print $1; exit}')"
[ -n "$WHOLE_DEVICE" ] && [ -n "$HFS_DEVICE" ] || {
    echo "could not locate the output image's HFS partition" >&2
    exit 1
}

WORK_DIR="$(mktemp -d "$GXMETAL_TMP_ROOT/gxmetal-ut-config.XXXXXX")"
diskutil mount -mountPoint "$WORK_DIR" "$HFS_DEVICE" >/dev/null
MOUNTED=1

UT_INI="$WORK_DIR/GXMetal Sweep Games/Unreal Tournament/UnrealTournament.ini"
[ -f "$UT_INI" ] || {
    echo "UnrealTournament.ini not found at the expected test-corpus path" >&2
    exit 1
}

python3 - "$UT_INI" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
data = path.read_bytes()
if b"\n" in data or data.count(b"\r") == 0:
    raise SystemExit("UnrealTournament.ini does not use classic Mac CR lines")

enabled = data.count(b"UseSound=True")
disabled = data.count(b"UseSound=False")
if enabled == 2 and disabled == 0:
    patched = data.replace(b"UseSound=True", b"UseSound=False")
elif enabled == 0 and disabled == 2:
    patched = data
else:
    raise SystemExit(
        f"unexpected UseSound entries: enabled={enabled} disabled={disabled}")

if patched != data:
    with path.open("r+b") as stream:
        stream.write(patched)
        stream.truncate()
        stream.flush()
if path.read_bytes().count(b"UseSound=False") != 2:
    raise SystemExit("UseSound verification failed")
PY

diskutil unmount "$HFS_DEVICE" >/dev/null
MOUNTED=0
hdiutil detach "$WHOLE_DEVICE" >/dev/null
WHOLE_DEVICE=""

AFTER_SOURCE_SHA="$(shasum -a 256 "$SOURCE_IMAGE" | awk '{print $1}')"
[ "$AFTER_SOURCE_SHA" = "$SOURCE_SHA" ] || {
    echo "source image changed unexpectedly" >&2
    exit 1
}

echo "Prepared Unreal Tournament test image: $OUTPUT_IMAGE"
echo "Source SHA-256: $SOURCE_SHA"
echo "Output SHA-256: $(shasum -a 256 "$OUTPUT_IMAGE" | awk '{print $1}')"
echo "UseSound=False (2 entries); fullscreen setting preserved"
