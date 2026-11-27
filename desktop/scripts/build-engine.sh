#!/usr/bin/env bash
# Native 64-bit Windows/MSYS2 UCRT64 build. All guest patches are shared.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
[ "${MSYSTEM:-}" = UCRT64 ] || { echo 'Run in MSYS2 UCRT64.' >&2; exit 1; }
CLASSICMAC_PREPARE_ONLY=1 PYTHON_BIN=python3 bash "$ROOT_DIR/scripts/build-qemu.sh"
QEMU_DIR="$ROOT_DIR/vendor/qemu"
python3 "$ROOT_DIR/desktop/scripts/prepare-windows-gl.py" "$QEMU_DIR"
mkdir -p "$QEMU_DIR/build-windows"
cd "$QEMU_DIR/build-windows"
../configure --target-list=m68k-softmmu,ppc-softmmu --python=python3 \
  --enable-tcg --enable-slirp --enable-vnc --enable-gcrypt --audio-drv-list=dsound \
  --disable-gtk --disable-sdl --disable-cocoa --disable-curses --disable-docs \
  --disable-guest-agent --disable-werror --disable-debug-info \
  --extra-cflags='-O2 -ffp-contract=off' --extra-ldflags='-lgdi32 -luser32'
ninja -j 3 qemu-system-m68k.exe qemu-system-ppc.exe qemu-img.exe
PYTHON_BIN=python3 bash "$ROOT_DIR/scripts/build-osx-qemu.sh"
python3 "$ROOT_DIR/desktop/scripts/stage-runtime.py" "$PWD" "$ROOT_DIR/output/desktop/runtime"
"$ROOT_DIR/output/desktop/runtime/bin/qemu-system-ppc.exe" -device ati-vga,help | grep x-gl-renderer
"$ROOT_DIR/output/desktop/runtime/bin/qemu-system-ppc.exe" -M help | grep -q '^pippin '
"$ROOT_DIR/output/desktop/runtime/bin/qemu-system-m68k.exe" -device nubus-qfb,help | grep width
VNC_HELP="$("$ROOT_DIR/output/desktop/runtime/bin/qemu-system-ppc.exe" -vnc help 2>&1 || true)"
grep -q "vnc options" <<< "$VNC_HELP"
