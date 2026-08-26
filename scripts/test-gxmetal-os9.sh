#!/usr/bin/env bash
#
# Run the GXMetal conformance application from the exact signed ClassicMac
# bundle against a disposable clone of a user-supplied Mac OS 9 disk.
# The source image is never attached or written.
#
# Usage:
#   scripts/test-gxmetal-os9.sh /path/to/mac-os-9-disk.img
#
# Optional environment:
#   GXMETAL_APP=/path/to/ClassicMac.app
#   GXMETAL_TOOLS_CD=/path/to/ClassicMacTools.iso
#   GXMETAL_QEMU=/path/to/qemu-system-ppc
#   GXMETAL_OS9_WAIT_SECONDS=90

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DISK="${1:-${OS9_DISK:-}}"
APP="${GXMETAL_APP:-$ROOT_DIR/dist/ClassicMac.app}"
WAIT_SECONDS="${GXMETAL_OS9_WAIT_SECONDS:-90}"
TOOLS_CD="${GXMETAL_TOOLS_CD:-$APP/Contents/Resources/ClassicMacTools.iso}"
QEMU="${GXMETAL_QEMU:-$APP/Contents/Helpers/Power Mac G4.app/Contents/MacOS/qemu-system-ppc}"
NDRVLOADER="$APP/Contents/Resources/ndrvloader"

SCRATCH=""
DISK=""
MONITOR=""
VNC=""
QEMU_PID=""
VNC_HELPER_PID=""
ATTACH_DEVICE=""
HFS_PARTITION=""
MOUNT_POINT=""
HFSUTILS_MOUNTED=0
SOURCE_SIZE=""
SOURCE_MTIME=""

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ "$HFSUTILS_MOUNTED" -eq 1 ]; then
    humount >/dev/null 2>&1 || true
  fi
  if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" >/dev/null 2>&1; then
    if [ -S "$MONITOR" ]; then
      printf 'quit\n' | nc -U "$MONITOR" >/dev/null 2>&1 || true
    fi
    kill "$QEMU_PID" >/dev/null 2>&1 || true
    wait "$QEMU_PID" >/dev/null 2>&1 || true
  fi
  if [ -n "$VNC_HELPER_PID" ] && \
     kill -0 "$VNC_HELPER_PID" >/dev/null 2>&1; then
    kill "$VNC_HELPER_PID" >/dev/null 2>&1 || true
    wait "$VNC_HELPER_PID" >/dev/null 2>&1 || true
  fi
  if [ -n "$HFS_PARTITION" ]; then
    diskutil unmount "$HFS_PARTITION" >/dev/null 2>&1 || true
  fi
  if [ -n "$ATTACH_DEVICE" ]; then
    diskutil eject "$ATTACH_DEVICE" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

for tool in codesign cp diskutil ditto hcopy hmount humount mktemp nc plutil \
            python3 sips stat unar; do
  command -v "$tool" >/dev/null 2>&1 || die "Required tool not found: $tool"
done
[ -n "$SOURCE_DISK" ] || die "Pass a Mac OS 9 disk image or set OS9_DISK."
[ -f "$SOURCE_DISK" ] || die "Mac OS 9 disk image not found: $SOURCE_DISK"
[ -d "$APP" ] || die "ClassicMac application not found: $APP"
[ -x "$QEMU" ] || die "Bundled Power Mac QEMU not found: $QEMU"
[ -f "$NDRVLOADER" ] || die "Bundled VGA NDRV loader not found: $NDRVLOADER"
[ -f "$TOOLS_CD" ] || die "Bundled ClassicMac Tools CD not found: $TOOLS_CD"
case "$WAIT_SECONDS" in
  ''|*[!0-9]*) die "GXMETAL_OS9_WAIT_SECONDS must be a positive integer." ;;
esac
[ "$WAIT_SECONDS" -gt 0 ] || \
  die "GXMETAL_OS9_WAIT_SECONDS must be a positive integer."
SOURCE_SIZE="$(stat -f '%z' "$SOURCE_DISK")"
SOURCE_MTIME="$(stat -f '%m' "$SOURCE_DISK")"

log "Verifying the exact signed application candidate"
"$ROOT_DIR/scripts/verify-release.sh" "$APP"

mkdir -p /tmp/gxmetal-validation
SCRATCH="$(mktemp -d /tmp/gxmetal-validation/os9-signed-conformance-XXXXXX)"
DISK="$SCRATCH/disk.img"
MONITOR="$SCRATCH/monitor.sock"
VNC="$SCRATCH/vnc.sock"

log "Cloning the source disk without modifying it"
if ! cp -c "$SOURCE_DISK" "$DISK" 2>/dev/null; then
  rm -f "$DISK"
  cp "$SOURCE_DISK" "$DISK"
fi
chmod u+w "$DISK"

if [ -n "${GXMETAL_TOOLS_CD:-}" ]; then
  log "Extracting the source candidate driver and test from $TOOLS_CD"
else
  log "Extracting the matching driver and test from the bundled Tools CD"
fi
mkdir -p "$SCRATCH/macbinary" "$SCRATCH/guest"
hmount "$TOOLS_CD" >/dev/null
HFSUTILS_MOUNTED=1
hcopy -m ':GXMetal:GXMetal' "$SCRATCH/macbinary/GXMetal.bin"
hcopy -m ':GXMetal:GXMetal Input' "$SCRATCH/macbinary/GXMetalInput.bin"
hcopy -m ':GXMetal:GXMetal Startup' "$SCRATCH/macbinary/GXMetalStartup.bin"
hcopy -m ':GXMetal:GXMetal Test' "$SCRATCH/macbinary/GXMetalTest.bin"
humount >/dev/null
HFSUTILS_MOUNTED=0
unar -quiet -output-directory "$SCRATCH/guest" \
  "$SCRATCH/macbinary/GXMetal.bin"
unar -quiet -output-directory "$SCRATCH/guest" \
  "$SCRATCH/macbinary/GXMetalInput.bin"
unar -quiet -output-directory "$SCRATCH/guest" \
  "$SCRATCH/macbinary/GXMetalStartup.bin"
unar -quiet -output-directory "$SCRATCH/guest" \
  "$SCRATCH/macbinary/GXMetalTest.bin"
STARTUP_GUEST_FILE="$SCRATCH/guest/GXMetal Startup"
if [ ! -e "$STARTUP_GUEST_FILE" ]; then
  STARTUP_GUEST_FILE="$SCRATCH/guest/GXMetalStartup"
fi
[ -e "$STARTUP_GUEST_FILE" ] || \
  die "GXMetal Startup did not decode from the bundled Tools CD"
INPUT_GUEST_FILE="$SCRATCH/guest/GXMetal Input"
if [ ! -e "$INPUT_GUEST_FILE" ]; then
  INPUT_GUEST_FILE="$SCRATCH/guest/GXMetalInput"
fi
[ -e "$INPUT_GUEST_FILE" ] || \
  die "GXMetal Input did not decode from the bundled Tools CD"

attach_disk() {
  local attach_output

  attach_output="$(diskutil image attach --noMount "$DISK")"
  ATTACH_DEVICE="$(printf '%s\n' "$attach_output" | awk 'NR == 1 { print $1 }')"
  HFS_PARTITION="$(printf '%s\n' "$attach_output" | \
    awk '$0 ~ /Apple_HFS/ { print $1; exit }')"
  [ -n "$ATTACH_DEVICE" ] && [ -n "$HFS_PARTITION" ] || \
    die "The cloned image does not contain a mountable Apple_HFS partition."
  diskutil mount "$HFS_PARTITION" >/dev/null
  MOUNT_POINT="$(diskutil info -plist "$HFS_PARTITION" | \
    plutil -extract MountPoint raw -o - -)"
  [ -d "$MOUNT_POINT/System Folder" ] || \
    die "The mounted clone does not contain a System Folder."
}

detach_disk() {
  diskutil unmount "$HFS_PARTITION" >/dev/null
  HFS_PARTITION=""
  MOUNT_POINT=""
  diskutil eject "$ATTACH_DEVICE" >/dev/null
  ATTACH_DEVICE=""
}

log "Installing GXMetal and its conformance app into the disposable clone"
attach_disk
BACKUP="$MOUNT_POINT/GXMetal Validation Backups/Signed Bundle Test $$"
mkdir -p "$BACKUP" "$MOUNT_POINT/System Folder/Startup Items"
if [ -e "$MOUNT_POINT/System Folder/Extensions/GXMetal" ]; then
  mv "$MOUNT_POINT/System Folder/Extensions/GXMetal" \
    "$BACKUP/GXMetal.previous"
fi
if [ -e "$MOUNT_POINT/System Folder/Extensions/GXMetal Startup" ]; then
  mv "$MOUNT_POINT/System Folder/Extensions/GXMetal Startup" \
    "$BACKUP/GXMetal Startup.previous"
fi
if [ -e "$MOUNT_POINT/System Folder/Extensions/GXMetal Input" ]; then
  mv "$MOUNT_POINT/System Folder/Extensions/GXMetal Input" \
    "$BACKUP/GXMetal Input.previous"
fi
if [ -e "$MOUNT_POINT/System Folder/Startup Items/GXMetal Test" ]; then
  mv "$MOUNT_POINT/System Folder/Startup Items/GXMetal Test" \
    "$BACKUP/GXMetal Test.previous"
fi
if [ -e "$MOUNT_POINT/System Folder/Preferences/GXMetal Test Results" ]; then
  mv "$MOUNT_POINT/System Folder/Preferences/GXMetal Test Results" \
    "$BACKUP/GXMetal Test Results.previous"
fi
ditto "$SCRATCH/guest/GXMetal" \
  "$MOUNT_POINT/System Folder/Extensions/GXMetal"
ditto "$STARTUP_GUEST_FILE" \
  "$MOUNT_POINT/System Folder/Extensions/GXMetal Startup"
ditto "$INPUT_GUEST_FILE" \
  "$MOUNT_POINT/System Folder/Extensions/GXMetal Input"
ditto "$SCRATCH/guest/GXMetal Test" \
  "$MOUNT_POINT/System Folder/Startup Items/GXMetal Test"
sync
detach_disk

log "Booting Mac OS 9 and running the in-guest conformance workload"
if [ -n "${GXMETAL_QEMU:-}" ]; then
  printf '    Source candidate QEMU: %s\n' "$QEMU"
fi
"$QEMU" \
  -d guest_errors \
  -accel tcg,tb-size=512 \
  -M mac99,via=cuda,audiodev=snd0 \
  -cpu 7400 \
  -m 512 \
  -L "$APP/Contents/Resources/qemu/pc-bios" \
  -display none \
  -vnc "unix:$VNC,share=force-shared" \
  -vga std \
  -global VGA.host-resize=on \
  -global VGA.vgamem_mb=64 \
  -global VGA.packed-lowbpp=on \
  -global VGA.untracked-vram=on \
  -global VGA.hardware-cursor=on \
  -global VGA.gxmetal=on \
  -global macio-ide.dma-completion-delay-ns=1000000 \
  -prom-env output-device=ttya \
  -g 640x480x15 \
  -name "GXMetal Signed Bundle OS 9 Conformance" \
  -device "loader,addr=0x4000000,file=$NDRVLOADER" \
  -device virtio-tablet-pci \
  -prom-env "boot-command=init-program go" \
  -audiodev none,id=snd0 \
  -monitor "unix:$MONITOR,server=on,wait=off" \
  -action reboot=shutdown \
  -drive "file=$DISK,format=raw,media=disk,index=0" \
  -nic none >"$SCRATCH/qemu.log" 2>&1 &
QEMU_PID=$!

for ((elapsed = 0; elapsed < 30; elapsed++)); do
  kill -0 "$QEMU_PID" >/dev/null 2>&1 || \
    die "QEMU exited before VNC became available; see $SCRATCH/qemu.log"
  [ -S "$VNC" ] && break
  sleep 1
done
[ -S "$VNC" ] || die "QEMU VNC socket was not created: $VNC"

# A source image retained after a forced test shutdown can legitimately boot
# into Mac OS 9's completed Disk First Aid warning. It blocks Startup Items,
# so dismiss only after a pixel in the 640x480 completion message has become
# black; the same location is grey while verification is still progressing.
# On a clean image the helper simply times out while GXMetal Test proceeds.
python3 "$ROOT_DIR/scripts/gxmetal-vnc.py" \
  --unix-socket "$VNC" \
  --wait-for-pixel 193,231,0,0,0,0 \
  --timeout "$((WAIT_SECONDS > 15 ? WAIT_SECONDS - 15 : WAIT_SECONDS))" \
  --poll-interval 1 \
  --click 500,264 \
  --delay 2 \
  --screenshot "$SCRATCH/after-disk-first-aid.png" \
  >"$SCRATCH/vnc-helper.log" 2>&1 &
VNC_HELPER_PID=$!

for ((elapsed = 0; elapsed < WAIT_SECONDS; elapsed++)); do
  kill -0 "$QEMU_PID" >/dev/null 2>&1 || \
    die "QEMU exited before the conformance wait completed; see $SCRATCH/qemu.log"
  sleep 1
done
if [ -n "$VNC_HELPER_PID" ]; then
  wait "$VNC_HELPER_PID" >/dev/null 2>&1 || true
  VNC_HELPER_PID=""
fi
[ -S "$MONITOR" ] || die "QEMU monitor was not created: $MONITOR"
printf 'screendump %s\n' "$SCRATCH/final.ppm" | \
  nc -U "$MONITOR" >/dev/null || true
if [ -f "$SCRATCH/final.ppm" ]; then
  sips -s format png "$SCRATCH/final.ppm" \
    --out "$SCRATCH/final.png" >/dev/null
fi
printf 'quit\n' | nc -U "$MONITOR" >/dev/null
wait "$QEMU_PID"
QEMU_PID=""

log "Reading the flushed result from the disposable guest disk"
attach_disk
RESULT_FILE="$MOUNT_POINT/System Folder/Preferences/GXMetal Test Results"
[ -f "$RESULT_FILE" ] || \
  die "GXMetal Test did not write a result; inspect $SCRATCH/final.png and qemu.log"
cp "$RESULT_FILE" "$SCRATCH/result.txt"
detach_disk

RESULT="$(cat "$SCRATCH/result.txt")"
case "$RESULT" in
  PASS:*) ;;
  *) die "OS 9 conformance failed: $RESULT" ;;
esac
[ "$(stat -f '%z' "$SOURCE_DISK")" = "$SOURCE_SIZE" ] && \
  [ "$(stat -f '%m' "$SOURCE_DISK")" = "$SOURCE_MTIME" ] || \
  die "The source disk's size or modification time changed during validation."

if [ -n "${GXMETAL_TOOLS_CD:-}${GXMETAL_QEMU:-}" ]; then
  log "OS 9 source-candidate conformance passed"
else
  log "OS 9 signed-bundle conformance passed"
fi
printf '    %s\n' "$RESULT"
printf '    Evidence: %s\n' "$SCRATCH"
