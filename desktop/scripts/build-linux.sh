#!/usr/bin/env bash
# Native Linux x86-64 engine. Shared guest code and firmware, EGL ATI backend.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
CLASSICMAC_PREPARE_ONLY=1 PYTHON_BIN=python3 bash "$ROOT_DIR/scripts/build-qemu.sh"
mkdir -p "$ROOT_DIR/vendor/qemu/build-linux"
cd "$ROOT_DIR/vendor/qemu/build-linux"
../configure --target-list=m68k-softmmu,ppc-softmmu --python=python3 \
  --enable-tcg --enable-slirp --enable-vnc --enable-gcrypt --enable-virtfs \
  --audio-drv-list=pa,alsa --disable-gtk --disable-sdl --disable-cocoa \
  --disable-curses --disable-docs --disable-guest-agent --disable-werror \
  --disable-debug-info --extra-cflags='-O2 -ffp-contract=off'
ninja -j "${CLASSICMAC_BUILD_JOBS:-4}" qemu-system-m68k qemu-system-ppc qemu-img
PYTHON_BIN=python3 bash "$ROOT_DIR/scripts/build-osx-qemu.sh"
python3 "$ROOT_DIR/desktop/scripts/stage-linux.py" "$PWD" "$ROOT_DIR/output/desktop/runtime"
