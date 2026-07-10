#!/usr/bin/env bash
# Boot the mac99 OS 9 install machine for the harness. Idempotent: recreates a
# fresh scratch disk each call unless KEEP_DISK=1.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
QEMU="$REPO/vendor/qemu/build/qemu-system-ppc"
BIOS="$REPO/vendor/qemu/pc-bios"
ISO="${ISO:-}"
DISK="${DISK:-/tmp/os9-harness/disk.img}"
QMP_SOCK="${QMP_SOCK:-/tmp/os9-harness/qmp.sock}"
MON_SOCK="${MON_SOCK:-/tmp/os9-harness/mon.sock}"
TRACE="${TRACE:-/tmp/os9-harness/trace.log}"
TRACE_EVENTS="${TRACE_EVENTS:-$REPO/tools/os9-install-harness/trace-events}"
MACIO_DELAY_NS="${MACIO_DELAY_NS:-1000000}"
CD_INDEX="${CD_INDEX:-1}"
AUDIODEV="${AUDIODEV:-none,id=snd0}"

[ -n "$ISO" ] || {
  printf 'ERROR: set ISO=/path/to/a Mac OS 9 install image\n' >&2
  exit 1
}
[ -f "$ISO" ] || {
  printf 'ERROR: install image not found: %s\n' "$ISO" >&2
  exit 1
}

mkdir -p "$(dirname "$DISK")" "$(dirname "$QMP_SOCK")" \
  "$(dirname "$MON_SOCK")" "$(dirname "$TRACE")"

if [ "${KEEP_DISK:-0}" != "1" ]; then
  rm -f "$DISK"
  "$REPO/vendor/qemu/build/qemu-img" create -f raw "$DISK" 4G >/dev/null
fi
rm -f "$QMP_SOCK" "$MON_SOCK" "$TRACE"

exec "$QEMU" \
  -M mac99,via=pmu,audiodev=snd0 \
  -m 512 \
  -L "$BIOS" \
  -display none \
  -vga std \
  -global VGA.host-resize=on \
  -global VGA.vgamem_mb=64 \
  -global VGA.packed-lowbpp=on \
  -global "macio-ide.dma-completion-delay-ns=$MACIO_DELAY_NS" \
  -prom-env output-device=ttya \
  -g 1024x768x32 \
  -name "OS9 Harness" \
  -audiodev "$AUDIODEV" \
  -qmp "unix:$QMP_SOCK,server=on,wait=off" \
  -monitor "unix:$MON_SOCK,server=on,wait=off" \
  -drive file="$DISK",format=raw,media=disk,index=0 \
  -drive file="$ISO",format=raw,media=cdrom,index="$CD_INDEX" \
  -boot d \
  -nic none \
  -trace "events=$TRACE_EVENTS" \
  -D "$TRACE"
