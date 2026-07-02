#!/usr/bin/env bash
#
# build-ppcvid-ndrv.sh - Build the ClassicMac PowerPC video driver
# (qemu_vga.ndrv) from the vendored driver source in ppcvid/driver, using the
# Retro68 powerpc-apple-macos cross toolchain plus Apple's Universal
# Interfaces (extracted from the MPW-GM disk image).
#
# The resulting driver is copied to ppcvid/qemu_vga.ndrv, where build-qemu.sh
# installs it into the QEMU tree (pc-bios/qemu_vga.ndrv), replacing the stock
# QemuMacDrivers build. It adds host-window-driven live resolution switching.
#
# This script is idempotent: Homebrew deps are installed only when missing,
# the Retro68 PPC toolchain pieces are built only when absent, the MPW image
# is downloaded and the Universal Interfaces installed only once, and the
# driver itself is rebuilt from clean on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
DRIVER_DIR="$ROOT_DIR/ppcvid/driver"
RETRO68_SRC="$VENDOR_DIR/Retro68"
RETRO68_BUILD="$VENDOR_DIR/Retro68-build"
RETRO68_TOOLCHAIN="$RETRO68_BUILD/toolchain"
RETRO68_REPO="${RETRO68_REPO:-https://github.com/autc04/Retro68.git}"
MPW_DIR="$VENDOR_DIR/mpw"
# MPW 3.5 Golden Master (MacBinary DiskCopy image) carrying the Universal
# Interfaces & Libraries; see the Retro68 README for known mirrors.
MPW_GM_URL="${MPW_GM_URL:-https://old.mac.gdn/apps/mpw-gm.img__0.bin}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Toolchain build dependencies via Homebrew
# ---------------------------------------------------------------------------
command -v brew >/dev/null 2>&1 || die "Homebrew is required. Install it from https://brew.sh"
BREW_PREFIX="$(brew --prefix)"

DEPS=(cmake gmp mpfr libmpc boost bison texinfo)
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
# 2. Retro68 source (shared with the 68k qfb toolchain)
# ---------------------------------------------------------------------------
if [ -d "$RETRO68_SRC/.git" ]; then
  log "Retro68 source already cloned"
else
  log "Cloning Retro68 (large: pulls gcc + binutils submodules)"
  mkdir -p "$VENDOR_DIR"
  git clone "$RETRO68_REPO" "$RETRO68_SRC"
  git -C "$RETRO68_SRC" submodule update --init --recursive
fi

# ---------------------------------------------------------------------------
# 3. PowerPC cross compilers (binutils + gcc), added alongside the 68k tools
# ---------------------------------------------------------------------------
# Built manually rather than via build-toolchain.bash because that script
# wipes the install prefix (which already holds the 68k toolchain).
JOBS="$(sysctl -n hw.ncpu)"
export CPPFLAGS="-I$BREW_PREFIX/include"
export LDFLAGS="-L$BREW_PREFIX/lib"

if [ -x "$RETRO68_TOOLCHAIN/bin/powerpc-apple-macos-as" ]; then
  log "PowerPC binutils already present"
else
  log "Building binutils (powerpc-apple-macos)"
  mkdir -p "$RETRO68_BUILD/binutils-build-ppc"
  (
    cd "$RETRO68_BUILD/binutils-build-ppc"
    "$RETRO68_SRC/binutils/configure" --disable-plugins \
      --target=powerpc-apple-macos --prefix="$RETRO68_TOOLCHAIN" \
      --disable-doc --disable-werror
    make -j"$JOBS"
    make install
  )
fi

if [ -x "$RETRO68_TOOLCHAIN/bin/powerpc-apple-macos-gcc" ]; then
  log "PowerPC gcc already present"
else
  log "Building gcc (powerpc-apple-macos); this can take a while"
  mkdir -p "$RETRO68_BUILD/gcc-build-ppc"
  (
    cd "$RETRO68_BUILD/gcc-build-ppc"
    export target_configargs="--disable-nls --enable-libstdcxx-dual-abi=yes --disable-libstdcxx-verbose"
    "$RETRO68_SRC/gcc/configure" --target=powerpc-apple-macos \
      --prefix="$RETRO68_TOOLCHAIN" \
      --enable-languages=c,c++ --disable-libssp --disable-lto MAKEINFO=missing
    make -j"$JOBS" || make
    make install
  )
fi

export PATH="$RETRO68_TOOLCHAIN/bin:$PATH"

# ---------------------------------------------------------------------------
# 4. Host tools (MakePEF, ConvertDiskImage, hfsutils) - built with the 68k
#    toolchain; only verified here.
# ---------------------------------------------------------------------------
[ -x "$RETRO68_TOOLCHAIN/bin/MakePEF" ] || die "MakePEF not found; run scripts/build-qfb-rom.sh first (it builds the Retro68 host tools)."

# ---------------------------------------------------------------------------
# 5. Apple Universal Interfaces (headers + import libraries) for PPC
# ---------------------------------------------------------------------------
# The driver needs Video.h, VideoServices.h, DriverServices.h etc., which the
# open-source multiversal interfaces do not provide. They are extracted from
# the MPW-GM image and linked into the powerpc-apple-macos target only; the
# 68k target keeps its multiversal setup (the qfb driver depends on it).
if [ -e "$RETRO68_TOOLCHAIN/powerpc-apple-macos/include/Video.h" ]; then
  log "Universal Interfaces already installed for the PPC target"
else
  if [ ! -d "$MPW_DIR/InterfacesAndLibraries/Interfaces" ]; then
    mkdir -p "$MPW_DIR"
    if [ ! -f "$MPW_DIR/MPW-GM.img.bin" ]; then
      log "Downloading MPW-GM (Universal Interfaces) from $MPW_GM_URL"
      curl -fL -o "$MPW_DIR/MPW-GM.img.bin" "$MPW_GM_URL" || die "Could not download MPW-GM.img.bin. Set MPW_GM_URL to a working mirror (see the Retro68 README)."
    fi
    log "Extracting Interfaces&Libraries from the MPW image"
    bash "$RETRO68_SRC/install-universal-interfaces.sh" "$MPW_DIR" MPW-GM.img.bin
  fi

  log "Installing Universal Interfaces for the PPC target"
  (
    export SRC="$RETRO68_SRC"
    export PREFIX="$RETRO68_TOOLCHAIN"
    export INTERFACES_DIR="$MPW_DIR/InterfacesAndLibraries"
    export BUILD_68K=false
    export BUILD_PPC=true
    export BUILD_CARBON=false
    export INTERFACES_KIND=universal
    source "$RETRO68_SRC/interfaces-and-libraries.sh"
    locateAndCheckInterfacesAndLibraries
    setUpInterfacesAndLibraries
    ln -sf ../RIncludes "$PREFIX/powerpc-apple-macos/RIncludes"
    removeConflictingHeaders "$PREFIX/powerpc-apple-macos/include"
    linkThings "../../universal/CIncludes" "$PREFIX/powerpc-apple-macos/include" "*.h"
    linkThings "../../universal/libppc" "$PREFIX/powerpc-apple-macos/lib" "*.a"
  )
fi

# ---------------------------------------------------------------------------
# 6. Retro68 PPC target runtime (libretrocrt, needed by the gcc driver link)
# ---------------------------------------------------------------------------
if [ -f "$RETRO68_TOOLCHAIN/powerpc-apple-macos/lib/libretrocrt.a" ]; then
  log "PPC target runtime already present"
else
  log "Building Retro68 PPC target runtime"
  mkdir -p "$RETRO68_BUILD/build-target-ppc"
  (
    cd "$RETRO68_BUILD/build-target-ppc"
    cmake "$RETRO68_SRC" \
      -DCMAKE_TOOLCHAIN_FILE=../build-host/cmake/intreeppc.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
  )
  cmake --build "$RETRO68_BUILD/build-target-ppc" --target install
fi

# ---------------------------------------------------------------------------
# 7. Build the driver
# ---------------------------------------------------------------------------
log "Building qemu_vga.ndrv from $DRIVER_DIR"
make -C "$DRIVER_DIR" clean
make -C "$DRIVER_DIR"

[ -f "$DRIVER_DIR/bin/qemu_vga.ndrv" ] || die "driver build did not produce bin/qemu_vga.ndrv"
cp "$DRIVER_DIR/bin/qemu_vga.ndrv" "$ROOT_DIR/ppcvid/qemu_vga.ndrv"
log "Installed ppcvid/qemu_vga.ndrv"

log "Done. Rebuild QEMU (scripts/build-qemu.sh) to install the new driver."
