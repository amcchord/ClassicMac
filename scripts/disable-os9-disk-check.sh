#!/usr/bin/env bash
#
# Persistently disable Mac OS 9's startup disk check through General Controls.
# The supplied disk image is modified in place. This is intended for a newly
# created, disposable/golden test image rather than an archival source image.
#
# Usage:
#   scripts/disable-os9-disk-check.sh /path/to/writable-os9.img
#
# Optional environment:
#   GXMETAL_APP=/path/to/ClassicMac.app
#   GXMETAL_QEMU=/path/to/qemu-system-ppc

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISK_IMAGE="${1:-}"
APP="${GXMETAL_APP:-$ROOT_DIR/dist/ClassicMac.app}"
QEMU="${GXMETAL_QEMU:-$APP/Contents/Helpers/Power Mac G4.app/Contents/MacOS/qemu-system-ppc}"
NDRVLOADER="$APP/Contents/Resources/ndrvloader"
FIRMWARE="$APP/Contents/Resources/qemu/pc-bios"

SCRATCH=""
QEMU_PID=""
MONITOR=""
VNC=""
ATTACH_DEVICE=""
HFS_PARTITION=""
SUCCESS=0

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

stop_qemu() {
  if [ -n "$QEMU_PID" ] && kill -0 "$QEMU_PID" >/dev/null 2>&1; then
    if [ -S "$MONITOR" ]; then
      printf 'quit\n' | nc -U "$MONITOR" >/dev/null 2>&1 || true
    fi
    wait "$QEMU_PID" >/dev/null 2>&1 || true
  fi
  QEMU_PID=""
}

cleanup() {
  stop_qemu
  if [ -n "$HFS_PARTITION" ]; then
    diskutil unmount "$HFS_PARTITION" >/dev/null 2>&1 || true
  fi
  if [ -n "$ATTACH_DEVICE" ]; then
    diskutil eject "$ATTACH_DEVICE" >/dev/null 2>&1 || true
  fi
  if [ "$SUCCESS" -eq 1 ] && [ -n "$SCRATCH" ] && [ -d "$SCRATCH" ]; then
    rm -rf "$SCRATCH"
  elif [ -n "$SCRATCH" ] && [ -d "$SCRATCH" ]; then
    printf '\nFailure evidence retained at: %s\n' "$SCRATCH" >&2
  fi
}
trap cleanup EXIT

for tool in diskutil mktemp nc plutil python3 xcrun; do
  command -v "$tool" >/dev/null 2>&1 || die "Required tool not found: $tool"
done
[ -n "$DISK_IMAGE" ] || die "Pass a writable Mac OS 9 disk image."
[ -f "$DISK_IMAGE" ] || die "Disk image not found: $DISK_IMAGE"
[ -w "$DISK_IMAGE" ] || die "Disk image is not writable: $DISK_IMAGE"
[ -x "$QEMU" ] || die "Power Mac QEMU not found: $QEMU"
[ -f "$NDRVLOADER" ] || die "NDRV loader not found: $NDRVLOADER"
[ -d "$FIRMWARE" ] || die "QEMU firmware directory not found: $FIRMWARE"

DISK_IMAGE="$(cd "$(dirname "$DISK_IMAGE")" && pwd)/$(basename "$DISK_IMAGE")"
# QEMU's Unix socket path limit is shorter than macOS's default per-user
# temporary directory. Keep this workspace deliberately short.
SCRATCH="$(mktemp -d /tmp/gxmetal-disk-check.XXXXXX)"

start_qemu() {
  local boot_id="$1"
  local elapsed

  MONITOR="$SCRATCH/$boot_id-monitor.sock"
  VNC="$SCRATCH/$boot_id-vnc.sock"
  "$QEMU" \
    -accel tcg,thread=multi \
    -M mac99,via=cuda,audiodev=snd0 \
    -cpu 7400 \
    -m 512 \
    -L "$FIRMWARE" \
    -display none \
    -vnc "unix:$VNC" \
    -vga std \
    -global VGA.host-resize=on \
    -global VGA.vgamem_mb=64 \
    -global VGA.packed-lowbpp=on \
    -global VGA.untracked-vram=on \
    -global VGA.hardware-cursor=on \
    -global VGA.gxmetal=off \
    -global macio-ide.dma-completion-delay-ns=1000000 \
    -prom-env output-device=ttya \
    -g 640x480x15 \
    -name "ClassicMac OS 9 Base Preparation" \
    -device "loader,addr=0x4000000,file=$NDRVLOADER" \
    -device virtio-tablet-pci \
    -prom-env "boot-command=init-program go" \
    -audiodev none,id=snd0 \
    -serial "file:$SCRATCH/$boot_id-serial.log" \
    -monitor "unix:$MONITOR,server=on,wait=off" \
    -action reboot=shutdown \
    -drive "file=$DISK_IMAGE,format=raw,media=disk,index=0" \
    -nic none >"$SCRATCH/$boot_id-qemu.log" 2>&1 &
  QEMU_PID=$!

  for ((elapsed = 0; elapsed < 30; elapsed++)); do
    kill -0 "$QEMU_PID" >/dev/null 2>&1 || \
      die "QEMU exited during $boot_id; see $SCRATCH/$boot_id-qemu.log"
    [ -S "$VNC" ] && [ -S "$MONITOR" ] && return
    sleep 1
  done
  die "QEMU sockets were not created during $boot_id."
}

vnc() {
  python3 "$ROOT_DIR/scripts/gxmetal-vnc.py" --unix-socket "$VNC" "$@"
}

wait_for_finder() {
  local timeout="$1"
  vnc --wait-for-pixel 22,11,221,0,0,8 --timeout "$timeout" \
    --poll-interval 1
}

log "Booting the writable golden image"
start_qemu configure

# A dirty source may be blocked by Disk First Aid. A clean source reaches the
# Finder directly. Probe the fast path first, then dismiss only the completed
# 640x480 warning; its completion-message pixel is grey while work continues.
if ! wait_for_finder 35 >"$SCRATCH/finder-first.log" 2>&1; then
  log "Waiting for the startup disk check to finish"
  # A source can finish its check and reach the Finder between the short
  # fast-path timeout above and the completion-dialog sample. Race both states
  # on one RFB connection so a clean/fast image cannot spend four minutes
  # waiting for a dialog that has already disappeared.
  python3 - "$ROOT_DIR/scripts/gxmetal-vnc.py" "$VNC" <<'PY'
import importlib.util
import socket
import sys
import time

module_path, vnc_path = sys.argv[1:]
spec = importlib.util.spec_from_file_location("gxmetal_vnc", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
connection.connect(vnc_path)
try:
    client = module.RFBClient(connection)
    client.connect()
    deadline = time.monotonic() + 240
    last_click = 0.0
    while True:
        rgb = client.capture()

        def pixel(x, y):
            offset = (y * client.width + x) * 3
            return tuple(rgb[offset:offset + 3])

        finder = pixel(22, 11)
        if all(abs(finder[i] - (221, 0, 0)[i]) <= 8 for i in range(3)):
            break
        now = time.monotonic()
        # Disk First Aid's completion-message sample becomes exactly black.
        # Repeated attempts are harmless during a still-black boot screen and
        # avoid losing the real dialog to a timing edge.
        if pixel(193, 231) == (0, 0, 0) and now - last_click >= 5:
            client.pointer_x = None
            client.pointer_y = None
            client.click(500, 264)
            last_click = now
        if now >= deadline:
            raise RuntimeError(
                "timed out waiting for either Disk First Aid completion "
                "or the Finder")
        time.sleep(1)
finally:
    connection.close()
PY
fi

log "Opening General Controls and collapsing the Control Strip"
# Keep a single RFB connection for this ordered UI sequence. QEMU translates
# the Power Mac's pointer movement relative to the connection's last position,
# so reconnecting between each click makes classic Mac OS coordinates drift.
python3 - "$ROOT_DIR/scripts/gxmetal-vnc.py" "$VNC" \
  "$SCRATCH/general-controls-disabled.png" <<'PY'
import importlib.util
import socket
import sys
import time

module_path, vnc_path, screenshot_path = sys.argv[1:]
spec = importlib.util.spec_from_file_location("gxmetal_vnc", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
connection.connect(vnc_path)
try:
    client = module.RFBClient(connection)
    client.connect()
    client.click(22, 11)
    time.sleep(1)
    client.click(100, 104)
    time.sleep(3)
    client.key("g", 0.15)
    time.sleep(1)
    client.double_click(303, 154)
    time.sleep(4)
    # Long classic-Mac pointer sequences can accumulate relative-position
    # drift. Re-home before the two pixel-critical controls.
    client.pointer_x = None
    client.pointer_y = None
    client.click(8, 468)
    time.sleep(1)

    # The checkbox interior is black when enabled and light grey when
    # disabled, making the operation idempotent instead of blindly toggling.
    try:
        module.wait_for_pixel(client, (108, 461, 0, 0, 0, 8), 3, 0.25)
    except RuntimeError:
        pass
    else:
        print("Check Disk is enabled; clearing it")
        client.pointer_x = None
        client.pointer_y = None
        client.click(109, 459)
        time.sleep(2)
    # Move the arrow cursor away before sampling the tiny checkbox interior.
    client.pointer_x = None
    client.pointer_y = None
    client.move_to(600, 400)
    module.wait_for_pixel(
        client, (108, 461, 221, 221, 221, 8), 10, 0.5)
    rgb = client.capture()
    module.write_png(screenshot_path, client.width, client.height, rgb)
    client.pointer_x = None
    client.pointer_y = None
    client.click(84, 69)
    time.sleep(2)
finally:
    connection.close()
PY
stop_qemu

log "Rebooting once to verify that Disk First Aid no longer blocks startup"
start_qemu verify
wait_for_finder 90
vnc --screenshot "$SCRATCH/reboot-finder.png"
stop_qemu

log "Verifying the General Controls preference resource"
ATTACH_OUTPUT="$(diskutil image attach --readOnly --noMount "$DISK_IMAGE")"
ATTACH_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | awk 'NR == 1 { print $1 }')"
HFS_PARTITION="$(printf '%s\n' "$ATTACH_OUTPUT" | \
  awk '$0 ~ /Apple_HFS/ { print $1; exit }')"
[ -n "$ATTACH_DEVICE" ] && [ -n "$HFS_PARTITION" ] || \
  die "The disk image does not contain an Apple_HFS partition."
diskutil mount readOnly "$HFS_PARTITION" >/dev/null
MOUNT_POINT="$(diskutil info -plist "$HFS_PARTITION" | \
  plutil -extract MountPoint raw -o - -)"
xcrun DeRez "$MOUNT_POINT/System Folder/Preferences/General Controls Prefs" \
  >"$SCRATCH/general-controls-prefs.r"
grep -Eq '\$"0000 0002"' "$SCRATCH/general-controls-prefs.r" || \
  die "General Controls Check Disk bit is still enabled."
diskutil unmount "$HFS_PARTITION" >/dev/null
HFS_PARTITION=""
diskutil eject "$ATTACH_DEVICE" >/dev/null
ATTACH_DEVICE=""

log "Mac OS 9 startup disk check disabled and reboot-verified"
printf '    Image: %s\n' "$DISK_IMAGE"
SUCCESS=1
