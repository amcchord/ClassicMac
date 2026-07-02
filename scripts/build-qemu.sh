#!/usr/bin/env bash
#
# build-qemu.sh - Build qemu-system-m68k (+ qemu-img) for Apple Silicon with both
# the enhanced nubus-qfb paravirtualized framebuffer (arbitrary resolutions +
# Thousands colour) and the nubus-virtio-mmio transport used for host folder
# sharing.
#
# Approach: clone mainline QEMU (which has nubus-virtio-mmio) at a pinned tag and
# port the nubus-qfb framebuffer onto it from files kept in qfb/.
#
# This script is idempotent: re-running it resets the tree to pristine, re-applies
# the qfb port, and performs an incremental rebuild.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
QEMU_DIR="$VENDOR_DIR/qemu"
BUILD_DIR="$QEMU_DIR/build"
QEMU_REPO="${QEMU_REPO:-https://gitlab.com/qemu-project/qemu.git}"
QEMU_TAG="${QEMU_TAG:-v11.0.2}"
QFB_DIR="$ROOT_DIR/qfb"

# Tracked files modified by the qfb and screamer integration patches (reset
# before re-applying).
PATCHED_FILES=(
  hw/display/macfb.c
  include/hw/display/macfb.h
  hw/m68k/q800.c
  include/hw/m68k/q800.h
  hw/display/Kconfig
  hw/m68k/Kconfig
  hw/display/meson.build
  pc-bios/meson.build
  ui/cocoa.m
  hw/audio/asc.c
  hw/audio/Kconfig
  hw/audio/meson.build
  hw/ppc/Kconfig
  hw/ppc/mac_newworld.c
  hw/ppc/mac_oldworld.c
  hw/misc/macio/macio.c
  include/hw/misc/macio/macio.h
  pc-bios/openbios-ppc
  hw/display/vga-pci.c
  hw/display/vga_int.h
  hw/display/virtio-vga.c
  include/hw/display/bochs-vbe.h
  pc-bios/qemu_vga.ndrv
)
SCREAMER_DIR="$ROOT_DIR/screamer"
PPCVID_DIR="$ROOT_DIR/ppcvid"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Toolchain + dependencies via Homebrew
# ---------------------------------------------------------------------------
command -v brew >/dev/null 2>&1 || die "Homebrew is required. Install it from https://brew.sh"

BREW_PREFIX="$(brew --prefix)"
# python@3.12 is pinned because QEMU's build tooling (mkvenv) is not compatible
# with Homebrew's bleeding-edge Python 3.14.
DEPS=(ninja meson pkg-config glib pixman dtc jpeg-turbo libpng libslirp dylibbundler python@3.12)
MISSING=()
for dep in "${DEPS[@]}"; do
  if ! brew list --versions "$dep" >/dev/null 2>&1; then
    MISSING+=("$dep")
  fi
done
if [ "${#MISSING[@]}" -gt 0 ]; then
  log "Installing Homebrew dependencies: ${MISSING[*]}"
  brew install "${MISSING[@]}"
else
  log "All Homebrew dependencies already installed"
fi

export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig:$BREW_PREFIX/opt/jpeg-turbo/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# QEMU's mkvenv requires the "distlib" module to be importable. Install it into a
# private directory exposed via PYTHONPATH (does not touch Homebrew site-packages).
PYTHON_BIN="$BREW_PREFIX/opt/python@3.12/bin/python3.12"
[ -x "$PYTHON_BIN" ] || die "python@3.12 not found at $PYTHON_BIN"
PYDEPS_DIR="$VENDOR_DIR/pydeps"
if [ ! -d "$PYDEPS_DIR/distlib" ]; then
  log "Installing distlib for the QEMU build tooling"
  mkdir -p "$PYDEPS_DIR"
  "$PYTHON_BIN" -m pip install --quiet --upgrade --target "$PYDEPS_DIR" distlib
fi
export PYTHONPATH="$PYDEPS_DIR:${PYTHONPATH:-}"

# ---------------------------------------------------------------------------
# 2. Clone mainline QEMU at the pinned tag
# ---------------------------------------------------------------------------
mkdir -p "$VENDOR_DIR"
if [ -d "$QEMU_DIR/.git" ]; then
  log "QEMU already present; resetting tracked files to pristine $QEMU_TAG"
  git -C "$QEMU_DIR" checkout -- "${PATCHED_FILES[@]}" 2>/dev/null || true
else
  log "Cloning $QEMU_REPO ($QEMU_TAG)"
  git clone --depth 1 --branch "$QEMU_TAG" "$QEMU_REPO" "$QEMU_DIR"
fi

# ---------------------------------------------------------------------------
# 2b. Optionally rebuild the enhanced framebuffer ROM/driver from source
# ---------------------------------------------------------------------------
# Off by default so routine QEMU builds stay fast and don't require the Retro68
# cross toolchain. Set QFB_BUILD_ROM=1 to regenerate qfb/mac_qfb.rom from the
# driver sources in qfb/driver before bundling it into the QEMU tree.
if [ -n "${QFB_BUILD_ROM:-}" ]; then
  log "QFB_BUILD_ROM set: rebuilding qfb/mac_qfb.rom from qfb/driver"
  "$ROOT_DIR/scripts/build-qfb-rom.sh"
fi

# Same pattern for the PPC video driver: set PPCVID_BUILD_NDRV=1 to regenerate
# ppcvid/qemu_vga.ndrv from the driver sources in ppcvid/driver (needs the
# Retro68 PPC toolchain plus the Universal Interfaces; see the script).
if [ -n "${PPCVID_BUILD_NDRV:-}" ]; then
  log "PPCVID_BUILD_NDRV set: rebuilding ppcvid/qemu_vga.ndrv from ppcvid/driver"
  "$ROOT_DIR/scripts/build-ppcvid-ndrv.sh"
fi

# ---------------------------------------------------------------------------
# 3. Apply the nubus-qfb framebuffer port
# ---------------------------------------------------------------------------
log "Installing nubus-qfb framebuffer (device files + firmware + integration patch)"
cp "$QFB_DIR/mac_qfb.c" "$QEMU_DIR/hw/display/mac_qfb.c"
cp "$QFB_DIR/mac_qfb.h" "$QEMU_DIR/include/hw/display/mac_qfb.h"
cp "$QFB_DIR/mac_qfb.rom" "$QEMU_DIR/pc-bios/mac_qfb.rom"
git -C "$QEMU_DIR" apply "$QFB_DIR/integration.patch" || die "Failed to apply qfb integration patch"
# Retina/HiDPI: size the Cocoa window at visual resolution rather than native pixels.
git -C "$QEMU_DIR" apply "$QFB_DIR/cocoa-retina.patch" || die "Failed to apply cocoa retina patch"
# Host-window-driven live resizing: make the Cocoa window resizable and feed
# window-size changes to the guest through the qfb device's ui_info hook.
git -C "$QEMU_DIR" apply "$QFB_DIR/cocoa-resize.patch" || die "Failed to apply cocoa resize patch"
# Apple Sound Chip: always feed the audio backend silence when idle so a live
# backend (CoreAudio) never replays stale ring-buffer content as a hum/buzz.
git -C "$QEMU_DIR" apply "$QFB_DIR/asc-silence.patch" || die "Failed to apply asc silence patch"

# ---------------------------------------------------------------------------
# 3b. Apply the screamer (AWACS) PPC Mac audio port
# ---------------------------------------------------------------------------
# Sound for the mac99/g3beige machines, ported from Mark Cave-Ayland's
# out-of-tree "screamer" branch. Needs the matching screamer-aware OpenBIOS
# (the guest driver only attaches when the firmware exposes the davbus/awacs
# nodes), which replaces the stock pc-bios/openbios-ppc.
log "Installing screamer PPC audio (device files + firmware + integration patch)"
cp "$SCREAMER_DIR/screamer.c" "$QEMU_DIR/hw/audio/screamer.c"
cp "$SCREAMER_DIR/screamer.h" "$QEMU_DIR/include/hw/audio/screamer.h"
cp "$SCREAMER_DIR/openbios-ppc" "$QEMU_DIR/pc-bios/openbios-ppc"
git -C "$QEMU_DIR" apply "$SCREAMER_DIR/integration.patch" || die "Failed to apply screamer integration patch"

# ---------------------------------------------------------------------------
# 3c. Apply the PPC std-VGA host-resize channel + custom video driver
# ---------------------------------------------------------------------------
# Adds a host->guest window-resize request channel to the std VGA device
# (host-resize=on) and replaces the stock qemu_vga.ndrv with the ClassicMac
# build that follows the host window via the Display Manager.
log "Installing PPC VGA host-resize support (patch + qemu_vga.ndrv)"
git -C "$QEMU_DIR" apply "$PPCVID_DIR/vga-host-resize.patch" || die "Failed to apply vga host-resize patch"
if [ -f "$PPCVID_DIR/qemu_vga.ndrv" ]; then
  cp "$PPCVID_DIR/qemu_vga.ndrv" "$QEMU_DIR/pc-bios/qemu_vga.ndrv"
else
  log "ppcvid/qemu_vga.ndrv not present; keeping the stock driver (no PPC live resize)"
fi

# ---------------------------------------------------------------------------
# 4. Configure (out-of-tree) if not already configured
# ---------------------------------------------------------------------------
if [ -f "$BUILD_DIR/build.ninja" ] && [ -z "${FORCE_CONFIGURE:-}" ]; then
  log "Already configured (set FORCE_CONFIGURE=1 to reconfigure)"
else
  log "Configuring QEMU for m68k-softmmu + ppc-softmmu (cocoa, slirp, coreaudio, 9p)"
  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"
  (
    cd "$BUILD_DIR"
    ../configure \
      --python="$PYTHON_BIN" \
      --target-list=m68k-softmmu,ppc-softmmu \
      --enable-cocoa \
      --enable-slirp \
      --enable-tcg \
      --audio-drv-list=coreaudio \
      --disable-werror \
      --disable-docs \
      --disable-gtk \
      --disable-sdl \
      --disable-vnc \
      --disable-curses \
      --disable-guest-agent \
      --disable-debug-info \
      --extra-cflags=-O2
  )
fi

# ---------------------------------------------------------------------------
# 5. Build
# ---------------------------------------------------------------------------
JOBS="$(sysctl -n hw.ncpu)"
log "Building with $JOBS jobs (this can take a while)"
ninja -C "$BUILD_DIR" qemu-system-m68k qemu-system-ppc qemu-img

# ---------------------------------------------------------------------------
# 6. Verify required devices are present
# ---------------------------------------------------------------------------
QEMU_BIN="$BUILD_DIR/qemu-system-m68k"
[ -x "$QEMU_BIN" ] || die "qemu-system-m68k was not produced"

log "QEMU version:"
"$QEMU_BIN" --version | head -1

for dev in nubus-qfb nubus-virtio-mmio virtio-9p-device; do
  if "$QEMU_BIN" -device help 2>&1 | grep -q "\"$dev\""; then
    printf '    OK  %s\n' "$dev"
  else
    die "device $dev missing from the build"
  fi
done

[ -f "$QEMU_DIR/pc-bios/mac_qfb.rom" ] || die "pc-bios/mac_qfb.rom firmware missing"

QEMU_PPC_BIN="$BUILD_DIR/qemu-system-ppc"
[ -x "$QEMU_PPC_BIN" ] || die "qemu-system-ppc was not produced"
if "$QEMU_PPC_BIN" -machine help 2>&1 | grep -q "^mac99"; then
  printf '    OK  mac99 machine (ppc)\n'
else
  die "mac99 machine missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device screamer,help >/dev/null 2>&1; then
  printf '    OK  screamer audio (ppc)\n'
else
  die "screamer audio device missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "host-resize"; then
  printf '    OK  VGA host-resize channel (ppc)\n'
else
  die "VGA host-resize property missing from the ppc build"
fi
if [ -f "$PPCVID_DIR/qemu_vga.ndrv" ] && cmp -s "$PPCVID_DIR/qemu_vga.ndrv" "$QEMU_DIR/pc-bios/qemu_vga.ndrv"; then
  printf '    OK  ClassicMac qemu_vga.ndrv installed\n'
else
  log "WARNING: pc-bios/qemu_vga.ndrv is not the ClassicMac build (PPC live resize inactive)"
fi
[ -f "$QEMU_DIR/pc-bios/openbios-ppc" ] || die "pc-bios/openbios-ppc firmware missing"
# Note: plain grep (not -q) so strings is read to EOF; grep -q would exit
# early and the SIGPIPE would fail the pipeline under pipefail.
if strings "$QEMU_DIR/pc-bios/openbios-ppc" | grep "screamer_init" >/dev/null; then
  printf '    OK  screamer-aware OpenBIOS\n'
else
  die "pc-bios/openbios-ppc is not the screamer-aware build"
fi

log "Done. Binaries: $BUILD_DIR/qemu-system-m68k , $BUILD_DIR/qemu-system-ppc , $BUILD_DIR/qemu-img"
