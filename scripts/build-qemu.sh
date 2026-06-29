#!/usr/bin/env bash
#
# build-qemu.sh - Build qemu-system-m68k (+ qemu-img) for Apple Silicon with the
# enhanced nubus-qfb paravirtualized framebuffer.
#
# The enhanced framebuffer (arbitrary resolutions + Thousands color) lives only
# in the SolraBizna QEMU fork, branch "arbitrary-resolutions". The firmware ships
# in that tree as pc-bios/mac_qfb.rom, so building this fork is all that is needed.
#
# This script is idempotent: re-running it updates the checkout and performs an
# incremental rebuild.
#
# Fallback (if the fork fails to build against a very new toolchain): cherry-pick
# the single nubus-qfb commit (f551de5) plus pc-bios/mac_qfb.rom onto current
# mainline QEMU and build that. See README for details.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
QEMU_DIR="$VENDOR_DIR/qemu"
BUILD_DIR="$QEMU_DIR/build"
QEMU_REPO="${QEMU_REPO:-https://github.com/SolraBizna/qemu.git}"
QEMU_BRANCH="${QEMU_BRANCH:-arbitrary-resolutions}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Toolchain + dependencies via Homebrew
# ---------------------------------------------------------------------------
command -v brew >/dev/null 2>&1 || die "Homebrew is required. Install it from https://brew.sh"

BREW_PREFIX="$(brew --prefix)"
# python@3.12 is pinned because this QEMU vintage's build tooling (mkvenv) is not
# compatible with Homebrew's bleeding-edge Python 3.14.
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

# Let pkg-config find both standard and keg-only brew libraries.
export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig:$BREW_PREFIX/opt/jpeg-turbo/lib/pkgconfig:$BREW_PREFIX/opt/libslirp/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

# QEMU's mkvenv requires the "distlib" module to be importable. Install it into a
# private directory and expose it via PYTHONPATH so we do not touch Homebrew's
# externally-managed site-packages. This is inherited by the venv QEMU creates.
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
# 2. Clone / update the QEMU fork
# ---------------------------------------------------------------------------
mkdir -p "$VENDOR_DIR"
if [ -d "$QEMU_DIR/.git" ]; then
  log "QEMU fork already present; updating $QEMU_BRANCH"
  git -C "$QEMU_DIR" fetch origin "$QEMU_BRANCH"
  git -C "$QEMU_DIR" checkout "$QEMU_BRANCH"
  git -C "$QEMU_DIR" reset --hard "origin/$QEMU_BRANCH"
else
  log "Cloning $QEMU_REPO ($QEMU_BRANCH)"
  git clone --single-branch --branch "$QEMU_BRANCH" "$QEMU_REPO" "$QEMU_DIR"
fi

# ---------------------------------------------------------------------------
# 3. Configure (out-of-tree) if not already configured
# ---------------------------------------------------------------------------
if [ -f "$BUILD_DIR/build.ninja" ] && [ -z "${FORCE_CONFIGURE:-}" ]; then
  log "Already configured (set FORCE_CONFIGURE=1 to reconfigure)"
else
  log "Configuring QEMU for m68k-softmmu (cocoa, slirp, coreaudio)"
  # Start from a clean build directory so a previously failed configure (e.g. a
  # stale pyvenv) does not poison the run.
  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"
  (
    cd "$BUILD_DIR"
    ../configure \
      --python="$PYTHON_BIN" \
      --target-list=m68k-softmmu \
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
# 4. Build
# ---------------------------------------------------------------------------
JOBS="$(sysctl -n hw.ncpu)"
log "Building with $JOBS jobs (this can take a while)"
ninja -C "$BUILD_DIR" qemu-system-m68k qemu-img

# ---------------------------------------------------------------------------
# 5. Verify the enhanced framebuffer is present
# ---------------------------------------------------------------------------
QEMU_BIN="$BUILD_DIR/qemu-system-m68k"
[ -x "$QEMU_BIN" ] || die "qemu-system-m68k was not produced"

log "QEMU version:"
"$QEMU_BIN" --version | head -1

if "$QEMU_BIN" -device help 2>&1 | grep -qi "nubus-qfb"; then
  log "SUCCESS: nubus-qfb enhanced framebuffer is available"
else
  die "nubus-qfb device missing - the build did not include the enhanced framebuffer"
fi

[ -f "$QEMU_DIR/pc-bios/mac_qfb.rom" ] || die "pc-bios/mac_qfb.rom firmware missing from the source tree"

log "Done. Binaries: $BUILD_DIR/qemu-system-m68k , $BUILD_DIR/qemu-img"
