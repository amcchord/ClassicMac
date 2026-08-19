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

# Compile for macOS 15 (Sequoia) and later, not just the host OS. clang reads
# this at every compile, so a full rebuild is needed for it to take effect on
# an existing build tree.
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor"
QEMU_DIR="$VENDOR_DIR/qemu"
BUILD_DIR="$QEMU_DIR/build"
QEMU_REPO="${QEMU_REPO:-https://gitlab.com/qemu-project/qemu.git}"
QEMU_TAG="${QEMU_TAG:-v11.0.2}"
QFB_DIR="$ROOT_DIR/qfb"
POWERMAC_DIR="$ROOT_DIR/powermac"
GXMETAL_DIR="$ROOT_DIR/gxmetal"

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
  qapi/ui.json
  hw/audio/asc.c
  audio/coreaudio.m
  hw/audio/Kconfig
  hw/audio/meson.build
  hw/ppc/Kconfig
  hw/ppc/mac_newworld.c
  hw/ppc/mac_oldworld.c
  hw/misc/macio/macio.c
  include/hw/misc/macio/macio.h
  hw/ide/macio.c
  hw/ide/trace-events
  hw/misc/macio/mac_dbdma.c
  hw/misc/macio/trace-events
  pc-bios/openbios-ppc
  hw/display/vga.c
  hw/display/vga-pci.c
  hw/display/vga_int.h
  hw/display/virtio-vga.c
  include/hw/display/bochs-vbe.h
  hw/block/virtio-blk.c
  include/hw/virtio/virtio-blk.h
  pc-bios/qemu_vga.ndrv
  hw/misc/macio/cuda.c
  include/hw/misc/macio/cuda.h
  hw/nvram/mac_nvram.c
  accel/tcg/cputlb.c
  accel/tcg/cpu-exec.c
  accel/tcg/icount-common.c
  accel/tcg/tb-maint.c
  accel/tcg/tcg-accel-ops-rr.c
  accel/tcg/tcg-accel-ops.c
  accel/tcg/tcg-accel-ops.h
  accel/tcg/tcg-runtime.h
  accel/tcg/translate-all.c
  hmp-commands.hx
  hw/ppc/ppc.c
  include/exec/icount.h
  include/exec/translation-block.h
  include/hw/ppc/ppc.h
  include/monitor/hmp.h
  include/system/cpu-timers.h
  include/tcg/tcg-op-common.h
  monitor/hmp-cmds.c
  system/cpu-timers.c
  target/ppc/mmu_helper.c
  target/ppc/translate.c
  tcg/tcg-op.c
)
SCREAMER_DIR="$ROOT_DIR/screamer"
PPCVID_DIR="$ROOT_DIR/ppcvid"
AUDIO_DIR="$ROOT_DIR/audio"
IDE_DIR="$ROOT_DIR/ide"
VIRTIO_DIR="$ROOT_DIR/virtio"

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

# Rebuild the classicvirtio declaration ROM and PowerPC NDRV loader when
# requested. This uses the same Retro68 toolchain.
if [ -n "${CLASSICVIRTIO_BUILD_ROM:-}" ]; then
  log "CLASSICVIRTIO_BUILD_ROM set: rebuilding shared classicvirtio drivers"
  "$ROOT_DIR/scripts/build-classicvirtio-floppy.sh"
fi

# Rebuild the patched PowerPC OpenBIOS firmware in its Linux cross-build
# container when requested. Routine QEMU builds use the committed binary.
if [ -n "${OPENBIOS_BUILD:-}" ]; then
  log "OPENBIOS_BUILD set: rebuilding screamer/openbios-ppc"
  "$ROOT_DIR/scripts/build-openbios.sh"
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
# ClassicMac UI skin: machine-named window titles and menus, no QEMU
# branding/Speed menu, friendly quit dialog, Dock icon from the helper bundle.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/classicmac-ui.patch" || die "Failed to apply ClassicMac cocoa UI patch"
# Classic Mac input remapping (both opt-in via -display cocoa options):
# right-click-ctrl delivers right clicks as Control+click (contextual menus)
# and scroll-keys turns wheel motion into arrow-key taps, since classic Mac
# OS mouse drivers ignore extra buttons and the wheel.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/input-remap.patch" || die "Failed to apply ClassicMac input remap patch"
# Machine menu items to insert/eject the bundled ClassicMac Tools CD into
# the dedicated tools0 drive while the Mac is running (shown when the app
# publishes the image path via CLASSICMAC_TOOLS_CD).
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/tools-cd-menu.patch" || die "Failed to apply ClassicMac tools CD menu patch"
# Mac-friendly names for the Removable Media menu items ("Change CD-ROM
# Image..." instead of "Change cd0...").
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/removable-media-names.patch" || die "Failed to apply ClassicMac removable media names patch"
# View menu checkbox to toggle the classic input helpers (right-click as
# Control+click, scroll wheel as arrow keys) while the machine runs.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/input-helpers-menu.patch" || die "Failed to apply ClassicMac input helpers menu patch"
# Control-Option-F fullscreen toggle (works even while input is grabbed, so
# fullscreen is easy to leave) and Control-Option-R to re-push the window's
# resolution to the guest, with View menu and Dock icon entries.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/fullscreen-resolution-menu.patch" || die "Failed to apply ClassicMac fullscreen/resolution menu patch"
# Control-Option-T borderless-window toggle plus final-frame reporting after
# native fullscreen transitions, so the guest uses the complete drawable area.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/window-presentation.patch" || die "Failed to apply ClassicMac window presentation patch"
# Final Mac-native menu pass: a state-aware Tools command, friendly Disc
# submenu and errors, focused View controls, and no raw emulator consoles or
# removable-device identifiers in the menu bar.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/mac-native-menus.patch" || die "Failed to apply ClassicMac native menus patch"
# Quadra-only floppy image controls for the removable classicvirtio drive.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/floppy-menu.patch" || die "Failed to apply ClassicMac floppy menu patch"
# Faster 32-bit Power Mac scanout: let Cocoa consume a big-endian framebuffer
# directly, and poll display updates at roughly the guest's 60 Hz VBL cadence.
git -C "$QEMU_DIR" apply "$ROOT_DIR/cocoaui/display-performance.patch" || die "Failed to apply Cocoa display performance patch"
# Allow a VirtIO block device to start empty, exchange raw media while running,
# and report capacity changes to the guest driver.
git -C "$QEMU_DIR" apply "$VIRTIO_DIR/virtio-blk-removable.patch" || die "Failed to apply removable VirtIO block patch"
# Apple Sound Chip: always feed the audio backend silence when idle so a live
# backend (CoreAudio) never replays stale ring-buffer content as a hum/buzz.
git -C "$QEMU_DIR" apply "$QFB_DIR/asc-silence.patch" || die "Failed to apply asc silence patch"
# CoreAudio backend: honor the channel count and sample rate the output
# device actually uses. Without this, guest stereo written to a multi-channel
# device (e.g. Studio Display speakers) is misread as N-channel frames and
# plays several times too fast (wrong pitch) with gaps (choppy).
git -C "$QEMU_DIR" apply "$AUDIO_DIR/coreaudio-device-format.patch" || die "Failed to apply coreaudio device format patch"

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

# CUDA timing/one-second messages and classic New World NVRAM partitions used
# by Mac OS 8.5 and 8.6 during early startup.
log "Installing classic Mac OS 8 Power Mac compatibility fixes"
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/classic-macos-8.patch" || die "Failed to apply Power Mac compatibility patch"
# Classic Mac graphics workloads write heavily around translated code. Keep a
# conservative per-page coverage summary so writes outside translated regions
# avoid walking the page's TB list, while preserving the original locked path
# for cross-page and otherwise ambiguous cases. Also keep Clang from expanding
# the small victim-TLB scan into hundreds of outlined helper calls.
log "Installing TCG graphics-workload fast path"
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/tcg-graphics-fast-path.patch" || die "Failed to apply TCG graphics fast-path patch"
# PowerPC BAT remaps cover contiguous guest-address ranges. Use QEMU's
# range-aware TLB invalidation so a BAT update takes one lock and one bounded
# scan instead of invalidating hundreds of pages independently. This keeps the
# exact invalidation semantics while materially shortening Mac OS 9 startup.
log "Installing PowerPC BAT range-invalidation fast path"
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/tcg-ppc-bat-range-flush.patch" || die "Failed to apply PowerPC BAT range-invalidation patch"
# Mac OS 9 reads the PowerPC timebase and returns through indirect branches
# unusually often during startup. The runtime patch accelerates those paths
# and adds ClassicMac's accounted instruction-clock to real-time handoff. The
# handoff preserves QEMU virtual time and the architectural 25 MHz timebase,
# then removes icount translation flags before Finder applications can run.
log "Installing Mac OS 9 PowerPC runtime fast paths"
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/os9-boot-runtime.patch" || die "Failed to apply Mac OS 9 runtime fast paths"
# Expose the exact host-time handoff instant for repeatable cold-boot metrics,
# and use the Power Mac's exact 40 ns timebase period instead of generic
# 128-bit scaling in Mac OS 9's heavily polled timebase path.
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/os9-boot-handoff-metrics.patch" || die "Failed to apply Mac OS 9 boot handoff metrics"
# MacIO IDE/DBDMA completion timing: cached host I/O can otherwise complete
# before classic Mac OS arms its synchronous wait, losing the wakeup and
# freezing Mac OS 9.2.x Installer mid-copy. This also fixes the ordinary ATA
# read path's accidental ATAPI completion callback and adds focused tracepoints
# for the MacIO/DBDMA state machine.
log "Installing MacIO IDE DMA race fix and tracepoints"
git -C "$QEMU_DIR" apply "$IDE_DIR/macio-ide-dma-race.patch" || die "Failed to apply MacIO IDE DMA race patch"

# Repaint the OpenBIOS console background from pale yellow (0xFFFFCC) to
# black. The firmware fills the whole framebuffer with that palette entry at
# video init, so even with the console routed to ttya the guest window shows
# a yellow screen until Mac OS takes over. The color exists exactly once in
# the binary (the Forth palette literal); with it black - and the firmware
# text already black - the display stays dark until the Mac OS boot screen.
# Idempotent: the pristine binary is re-copied from screamer/ above on every
# run, and the patcher accepts an already-patched file.
log "Patching OpenBIOS console background to black"
"$PYTHON_BIN" - "$QEMU_DIR/pc-bios/openbios-ppc" <<'PYEOF' || die "Failed to patch OpenBIOS background color"
import sys

path = sys.argv[1]
with open(path, "rb") as f:
    data = f.read()

yellow = bytes.fromhex("00ffffcc")
count = data.count(yellow)
if count == 1:
    with open(path, "wb") as f:
        f.write(data.replace(yellow, bytes(4)))
    print("    OK  OpenBIOS background palette entry set to black")
elif count == 0:
    # Nothing to patch: only acceptable if a previous run already did it,
    # which we cannot distinguish here; treat as an error so an OpenBIOS
    # update that changes the constant is caught instead of silently
    # shipping yellow screens again.
    sys.exit("openbios-ppc: background color constant 0x00FFFFCC not found")
else:
    sys.exit(f"openbios-ppc: expected 1 occurrence of 0x00FFFFCC, found {count}")
PYEOF

# ---------------------------------------------------------------------------
# 3c. Apply the PPC std-VGA host-resize channel + custom video driver
# ---------------------------------------------------------------------------
# Adds a host->guest window-resize request channel to the std VGA device
# (host-resize=on) and replaces the stock qemu_vga.ndrv with the ClassicMac
# build that follows the host window via the Display Manager.
log "Installing PPC VGA host-resize support (patch + qemu_vga.ndrv)"
git -C "$QEMU_DIR" apply "$PPCVID_DIR/vga-host-resize.patch" || die "Failed to apply vga host-resize patch"
# Packed linear 1/2/4-bpp indexed modes (packed-lowbpp=on) so the Mac OS
# Monitors control panel can offer Black & White, 4 and 16 colors. Applied
# on top of the host-resize patch (extends its QEXT register block with a
# feature bitmap the guest driver probes).
git -C "$QEMU_DIR" apply "$PPCVID_DIR/vga-packed-depths.patch" || die "Failed to apply vga packed-depths patch"
# QEXT host-composited cursor channel used by the bundled PowerPC NDRV. This
# keeps pointer movement out of the guest framebuffer and Cocoa scanout path.
git -C "$QEMU_DIR" apply "$PPCVID_DIR/vga-hardware-cursor.patch" || die "Failed to apply vga hardware-cursor patch"
# Share Cocoa-compatible 32-bpp VRAM directly and stop clearing VGA's dirty
# bitmap only for genuinely shared surfaces. Big-endian RGB555/RGB565 keep
# QEMU's converted shadow path because Core Graphics cannot safely consume the
# Pixman layouts directly.
git -C "$QEMU_DIR" apply "$PPCVID_DIR/vga-fast-scanout.patch" || die "Failed to apply vga fast-scanout patch"

# GXMetal's RAVE transport extends std-VGA with a validated command queue in a
# separate shared PCI BAR. Metal is selected on supported macOS hosts, with the
# portable reference rasterizer retained as a deterministic fallback.
log "Installing GXMetal command transport"
cp "$GXMETAL_DIR/protocol/gxmetal_protocol.h" "$QEMU_DIR/hw/display/gxmetal_protocol.h"
cp "$GXMETAL_DIR/host/gxmetal_decode.h" "$QEMU_DIR/hw/display/gxmetal_decode.h"
cp "$GXMETAL_DIR/host/gxmetal_decode.c" "$QEMU_DIR/hw/display/gxmetal_decode.c"
cp "$GXMETAL_DIR/host/gxmetal_dirty.h" "$QEMU_DIR/hw/display/gxmetal_dirty.h"
cp "$GXMETAL_DIR/host/gxmetal_dirty.c" "$QEMU_DIR/hw/display/gxmetal_dirty.c"
cp "$GXMETAL_DIR/host/gxmetal_queue.h" "$QEMU_DIR/hw/display/gxmetal_queue.h"
cp "$GXMETAL_DIR/host/gxmetal_queue.c" "$QEMU_DIR/hw/display/gxmetal_queue.c"
cp "$GXMETAL_DIR/host/gxmetal_metal.h" "$QEMU_DIR/hw/display/gxmetal_metal.h"
cp "$GXMETAL_DIR/host/gxmetal_metal.m" "$QEMU_DIR/hw/display/gxmetal_metal.m"
cp "$GXMETAL_DIR/host/gxmetal_metal_stub.c" "$QEMU_DIR/hw/display/gxmetal_metal_stub.c"
cp "$GXMETAL_DIR/host/gxmetal_renderer.h" "$QEMU_DIR/hw/display/gxmetal_renderer.h"
cp "$GXMETAL_DIR/host/gxmetal_renderer.c" "$QEMU_DIR/hw/display/gxmetal_renderer.c"
cp "$GXMETAL_DIR/qemu/gxmetal_qemu.h" "$QEMU_DIR/hw/display/gxmetal_qemu.h"
cp "$GXMETAL_DIR/qemu/gxmetal_qemu.c" "$QEMU_DIR/hw/display/gxmetal_qemu.c"
git -C "$QEMU_DIR" apply "$GXMETAL_DIR/qemu-integration.patch" || die "Failed to apply GXMetal integration patch"
# Recognize Finder's menu bar directly in guest VRAM so startup acceleration
# ends automatically even in headless/VNC sessions and at arbitrary supported
# resolutions. The app keeps a conservative HMP fallback for custom themes.
git -C "$QEMU_DIR" apply "$POWERMAC_DIR/classicmac-boot-handoff-display.patch" || die "Failed to apply ClassicMac Finder handoff detector"
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
  log "Configuring QEMU for m68k-softmmu + ppc-softmmu (cocoa, VNC, slirp, coreaudio, 9p)"
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
      --enable-vnc \
      --disable-curses \
      --disable-guest-agent \
      --disable-debug-info \
      --enable-lto \
      --extra-cflags=-O3 \
      --extra-cxxflags=-O3 \
      --extra-objcflags=-O3
  )
  "$BUILD_DIR/pyvenv/bin/meson" configure "$BUILD_DIR" \
    -Doptimization=3 -Db_lto_mode=thin
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

if "$QEMU_BIN" -device virtio-blk-device,help 2>&1 | grep "removable" >/dev/null; then
  printf '    OK  removable virtio-blk floppy backend\n'
else
  die "removable virtio-blk property missing from the build"
fi

[ -f "$QEMU_DIR/pc-bios/mac_qfb.rom" ] || die "pc-bios/mac_qfb.rom firmware missing"

QEMU_PPC_BIN="$BUILD_DIR/qemu-system-ppc"
[ -x "$QEMU_PPC_BIN" ] || die "qemu-system-ppc was not produced"
grep -Eq '^#define CONFIG_VNC([[:space:]]|$)' \
  "$BUILD_DIR/config-host.h" || \
  die "QEMU was built without the required headless VNC display"
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
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "packed-lowbpp"; then
  printf '    OK  VGA packed low-bpp modes (ppc)\n'
else
  die "VGA packed-lowbpp property missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "hardware-cursor"; then
  printf '    OK  VGA hardware cursor channel (ppc)\n'
else
    die "VGA hardware-cursor property missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "untracked-vram"; then
  printf '    OK  VGA untracked direct scanout (ppc)\n'
else
  die "VGA untracked-vram property missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "classicmac-boot-handoff" && \
   strings "$QEMU_PPC_BIN" | grep "classicmac-boot-complete" >/dev/null; then
  printf '    OK  Mac OS 9 automatic Finder clock handoff (ppc)\n'
else
  die "Mac OS 9 automatic Finder clock handoff missing from the ppc build"
fi
if "$QEMU_PPC_BIN" -device VGA,help 2>&1 | grep -q "gxmetal"; then
  printf '    OK  GXMetal command transport (ppc)\n'
else
  die "VGA gxmetal property missing from the ppc build"
fi
if nm "$QEMU_PPC_BIN" 2>/dev/null | grep "_gxmetal_metal_dispatch" >/dev/null &&
   otool -L "$QEMU_PPC_BIN" | grep "/Metal.framework/" >/dev/null; then
  printf '    OK  GXMetal native Metal renderer (ppc)\n'
else
  die "GXMetal Metal renderer missing from the ppc build"
fi
"$PYTHON_BIN" "$GXMETAL_DIR/tests/test_qemu_transport.py" "$QEMU_PPC_BIN" || \
  die "GXMetal QEMU realization tests failed"
if "$QEMU_PPC_BIN" -device macio-ide,help 2>&1 | grep -q "dma-completion-delay-ns"; then
  printf '    OK  MacIO IDE DMA completion delay (ppc)\n'
else
  die "MacIO IDE DMA completion delay property missing from the ppc build"
fi
if [ -f "$PPCVID_DIR/qemu_vga.ndrv" ] && cmp -s "$PPCVID_DIR/qemu_vga.ndrv" "$QEMU_DIR/pc-bios/qemu_vga.ndrv"; then
  printf '    OK  ClassicMac qemu_vga.ndrv installed\n'
else
  log "WARNING: pc-bios/qemu_vga.ndrv is not the ClassicMac build (PPC live resize inactive)"
fi
[ -f "$QEMU_DIR/pc-bios/openbios-ppc" ] || die "pc-bios/openbios-ppc firmware missing"
# Note: plain grep (not -q) so strings is read to EOF; grep -q would exit
# early and the SIGPIPE would fail the pipeline under pipefail.
if strings "$QEMU_DIR/pc-bios/openbios-ppc" | grep "screamer" >/dev/null; then
  printf '    OK  screamer-aware OpenBIOS\n'
else
  die "pc-bios/openbios-ppc is not the screamer-aware build"
fi
if strings "$QEMU_DIR/pc-bios/openbios-ppc" | grep "iMac,1" >/dev/null && \
   strings "$QEMU_DIR/pc-bios/openbios-ppc" | grep "nvram-fetch" >/dev/null; then
  printf '    OK  classic Mac OS 8 OpenBIOS profile\n'
else
  die "pc-bios/openbios-ppc is missing classic Mac OS 8 compatibility"
fi

log "Done. Binaries: $BUILD_DIR/qemu-system-m68k , $BUILD_DIR/qemu-system-ppc , $BUILD_DIR/qemu-img"
