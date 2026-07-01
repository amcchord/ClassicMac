#!/usr/bin/env bash
#
# build-qfb-rom.sh - Build the enhanced nubus-qfb declaration ROM + 68k driver
# (mac_qfb.rom) from the vendored driver source in qfb/driver, using the
# Retro68 m68k-apple-macos cross toolchain.
#
# The resulting ROM is copied to qfb/mac_qfb.rom, where build-qemu.sh installs
# it into the QEMU tree (pc-bios/mac_qfb.rom).
#
# This script is idempotent: Homebrew deps are installed only when missing, the
# Retro68 toolchain is cloned/built only when absent, and the driver is rebuilt
# from clean on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
DRIVER_DIR="$ROOT_DIR/qfb/driver"
RETRO68_SRC="$VENDOR_DIR/Retro68"
RETRO68_BUILD="$VENDOR_DIR/Retro68-build"
RETRO68_TOOLCHAIN="$RETRO68_BUILD/toolchain"
RETRO68_REPO="${RETRO68_REPO:-https://github.com/autc04/Retro68.git}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Toolchain build dependencies via Homebrew
# ---------------------------------------------------------------------------
command -v brew >/dev/null 2>&1 || die "Homebrew is required. Install it from https://brew.sh"
BREW_PREFIX="$(brew --prefix)"

# lua is needed by the driver makefile to stamp the ROM checksum; the rest are
# Retro68's build prerequisites.
DEPS=(cmake gmp mpfr libmpc boost bison texinfo lua)
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

# Homebrew bison is keg-only; Retro68 needs a newer bison than macOS ships.
export PATH="$BREW_PREFIX/opt/bison/bin:$PATH"

# ---------------------------------------------------------------------------
# 2. Retro68 cross toolchain (m68k-apple-macos)
# ---------------------------------------------------------------------------
if [ -x "$RETRO68_TOOLCHAIN/bin/m68k-apple-macos-gcc" ]; then
  log "Retro68 toolchain already present at $RETRO68_TOOLCHAIN"
else
  mkdir -p "$VENDOR_DIR"
  if [ -d "$RETRO68_SRC/.git" ]; then
    log "Retro68 source already cloned; ensuring submodules are present"
    git -C "$RETRO68_SRC" submodule update --init --recursive
  else
    log "Cloning Retro68 (large: pulls gcc + binutils submodules)"
    git clone "$RETRO68_REPO" "$RETRO68_SRC"
    git -C "$RETRO68_SRC" submodule update --init --recursive
  fi
  log "Building Retro68 toolchain (68k only; this can take a long time)"
  mkdir -p "$RETRO68_BUILD"
  (
    cd "$RETRO68_BUILD"
    # Only the classic 68k Mac toolchain is needed for this driver.
    "$RETRO68_SRC/build-toolchain.bash" --no-ppc --no-carbon
  )
fi

[ -x "$RETRO68_TOOLCHAIN/bin/m68k-apple-macos-gcc" ] || \
  die "Retro68 toolchain build did not produce m68k-apple-macos-gcc"

export PATH="$RETRO68_TOOLCHAIN/bin:$PATH"

# ---------------------------------------------------------------------------
# 3. Build the driver ROM
# ---------------------------------------------------------------------------
log "Building mac_qfb.rom from $DRIVER_DIR"
make -C "$DRIVER_DIR" clean
make -C "$DRIVER_DIR"

if [ -f "$DRIVER_DIR/bin/mac_qfb.rom" ]; then
  cp "$DRIVER_DIR/bin/mac_qfb.rom" "$ROOT_DIR/qfb/mac_qfb.rom"
  log "Installed qfb/mac_qfb.rom (checksum-stamped ROM)"
elif [ -f "$DRIVER_DIR/bin/mac_qfb.bin" ]; then
  # No Lua: the ROM lacks a valid checksum, but QEMU fixes it at load time.
  cp "$DRIVER_DIR/bin/mac_qfb.bin" "$ROOT_DIR/qfb/mac_qfb.rom"
  log "Installed qfb/mac_qfb.rom from .bin (QEMU will repair the checksum)"
else
  die "driver build did not produce bin/mac_qfb.rom or bin/mac_qfb.bin"
fi

log "Done. Rebuild QEMU (scripts/build-qemu.sh) to install the new ROM."
