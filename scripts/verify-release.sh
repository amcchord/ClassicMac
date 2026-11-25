#!/usr/bin/env bash
#
# verify-release.sh - Verify the exact ClassicMac application or notarized DMG
# that will be handed to a tester.  A DMG check mounts the image read-only and
# validates both the image and the application inside it.
#
# Usage:
#   scripts/verify-release.sh [app-or-dmg] [short-version] [build-version]
#
# Examples:
#   scripts/verify-release.sh dist/ClassicMac.app 4.7.0 4.7.0
#   scripts/verify-release.sh dist/ClassicMac.dmg 4.7.0 4.7.0
#
# Environment: APP_VERSION / APP_BUILD_VERSION stand in for the version
# arguments; DIST_DIR and DIST_NAME (defaults dist/ and ClassicMac) give the
# default target DIST_DIR/DIST_NAME.dmg; TOOLS_CD_SRC (default
# dist/ClassicMacTools.iso, as in bundle-qemu.sh) is the freshly built Tools
# CD compared with the bundled one; VERIFY_RELEASE_SKIP_REPO_FRESHNESS=1 skips
# comparisons against files in the source checkout.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
DIST_NAME="${DIST_NAME:-ClassicMac}"
TARGET="${1:-$DIST_DIR/$DIST_NAME.dmg}"
EXPECTED_VERSION="${2:-${APP_VERSION:-}}"
EXPECTED_BUILD="${3:-${APP_BUILD_VERSION:-}}"
SKIP_REPO_FRESHNESS="${VERIFY_RELEASE_SKIP_REPO_FRESHNESS:-0}"
MOUNT_ROOT=""
ATTACH_DEVICE=""

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup() {
  if [ -n "$ATTACH_DEVICE" ]; then
    diskutil eject "$ATTACH_DEVICE" >/dev/null 2>&1 || true
  fi
  if [ -n "$MOUNT_ROOT" ] && [ -d "$MOUNT_ROOT" ]; then
    rmdir "$MOUNT_ROOT" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

for tool in cmp codesign file plutil shasum spctl strings xcrun; do
  command -v "$tool" >/dev/null 2>&1 || die "Required tool not found: $tool"
done
[ -e "$TARGET" ] || die "Release target not found: $TARGET"

APP="$TARGET"
VERIFY_NOTARIZATION=0
case "$TARGET" in
  *.dmg)
    VERIFY_NOTARIZATION=1
    log "Validating the DMG notarization ticket and Gatekeeper policy"
    xcrun stapler validate "$TARGET"
    spctl --assess --type open --context context:primary-signature \
      --verbose=2 "$TARGET"

    MOUNT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/classicmac-release.XXXXXX")"
    ATTACH_OUTPUT="$(diskutil image attach --readOnly --nobrowse \
      --mountPoint "$MOUNT_ROOT" "$TARGET")"
    ATTACH_DEVICE="$(printf '%s\n' "$ATTACH_OUTPUT" | awk 'NR == 1 { print $1 }')"
    [ -n "$ATTACH_DEVICE" ] || die "Could not determine the attached DMG device"
    APP="$MOUNT_ROOT/ClassicMac.app"
    [ -d "$APP" ] || die "ClassicMac.app is missing from the DMG root"
    ;;
  *.app)
    [ -d "$APP" ] || die "Application bundle not found: $APP"
    ;;
  *)
    die "Expected a ClassicMac .app or .dmg target"
    ;;
esac

PLIST="$APP/Contents/Info.plist"
MAIN_BIN="$APP/Contents/MacOS/ClassicMac"
PPC_HELPER="$APP/Contents/Helpers/Power Mac G4.app"
PPC_QEMU="$PPC_HELPER/Contents/MacOS/qemu-system-ppc"
QUADRA_HELPER="$APP/Contents/Helpers/Quadra 800.app"
QUADRA_QEMU="$QUADRA_HELPER/Contents/MacOS/qemu-system-m68k"
QUADRA_QEMU_IMG="$QUADRA_HELPER/Contents/MacOS/qemu-img"
OSX_HELPER="$APP/Contents/Helpers/Power Mac G4 for OS X.app"
OSX_QEMU="$OSX_HELPER/Contents/MacOS/qemu-system-ppc"
COPLAND_HELPER="$APP/Contents/Helpers/Power Mac 7500.app"
COPLAND_ENGINE="$COPLAND_HELPER/Contents/MacOS/dingusppc"
PPC_NDRV="$APP/Contents/Resources/qemu/pc-bios/qemu_vga.ndrv"
DECLROM="$APP/Contents/Resources/declrom"
NDRVLOADER="$APP/Contents/Resources/ndrvloader"
TOOLS_CD="$APP/Contents/Resources/ClassicMacTools.iso"
VNC_KEYMAP="$APP/Contents/Resources/qemu/pc-bios/keymaps/en-us"
BROWSER_INDEX="$APP/Contents/Resources/Browser/index.html"
BROWSER_RFB="$APP/Contents/Resources/Browser/novnc/core/rfb.js"
BROWSER_SCALE="$APP/Contents/Resources/Browser/pixel-scale.js"
BROWSER_LICENSE="$APP/Contents/Resources/Licenses/noVNC-MPL-2.0.txt"
PAKO_LICENSE="$APP/Contents/Resources/Licenses/pako-MIT.txt"
NOTICES_BUNDLE="$APP/Contents/Resources/ThirdPartyNotices.md"
ASSETS_CAR="$APP/Contents/Resources/Assets.car"

for required in "$MAIN_BIN" "$COPLAND_HELPER/Contents/Info.plist" "$COPLAND_ENGINE" \
  "$PLIST" "$PPC_HELPER/Contents/Info.plist" "$QUADRA_HELPER/Contents/Info.plist" \
  "$PPC_QEMU" "$QUADRA_QEMU" "$QUADRA_QEMU_IMG" "$PPC_NDRV" "$DECLROM" "$NDRVLOADER" \
  "$TOOLS_CD" "$VNC_KEYMAP" "$BROWSER_INDEX" "$BROWSER_SCALE" "$BROWSER_RFB" \
  "$BROWSER_LICENSE" "$PAKO_LICENSE" "$NOTICES_BUNDLE" "$ASSETS_CAR"; do
  [ -e "$required" ] || die "Required release component is missing: $required"
done
[ -s "$TOOLS_CD" ] || die "Bundled ClassicMac Tools CD is empty"
[ -s "$ASSETS_CAR" ] || die "Bundled Assets.car (Liquid Glass icon) is empty"
ICON_NAME="$(plutil -extract CFBundleIconName raw "$PLIST" 2>/dev/null || true)"
[ "$ICON_NAME" = "AppIcon" ] || die "CFBundleIconName missing or not AppIcon (found: ${ICON_NAME:-none})"
if [ -n "$(find "$APP" -name '._*' -print -quit)" ]; then
  die "Bundle contains AppleDouble ._ metadata files"
fi

TOOLS_CD_SRC="${TOOLS_CD_SRC:-$ROOT_DIR/dist/ClassicMacTools.iso}"
if [ "$SKIP_REPO_FRESHNESS" != "1" ] && \
   [ -f "$TOOLS_CD_SRC" ]; then
  cmp -s "$TOOLS_CD_SRC" "$TOOLS_CD" || \
    die "Bundled Tools CD differs from the freshly built dist image"
fi
if [ "$SKIP_REPO_FRESHNESS" != "1" ] && \
   [ -f "$ROOT_DIR/ppcvid/qemu_vga.ndrv" ]; then
  cmp -s "$ROOT_DIR/ppcvid/qemu_vga.ndrv" "$PPC_NDRV" || \
    die "Bundled Power Mac NDRV differs from the freshly built driver"
fi
if [ "$SKIP_REPO_FRESHNESS" != "1" ]; then
  cmp -s "$ROOT_DIR/qfb/mac_qfb.rom" "$APP/Contents/Resources/qemu/pc-bios/mac_qfb.rom" || \
    die "Bundled Quadra ROM differs from the tracked driver"
  cmp -s "$ROOT_DIR/shared/declrom" "$DECLROM" || \
    die "Bundled Quadra VirtIO ROM (declrom) differs from the tracked firmware"
  cmp -s "$ROOT_DIR/shared/ndrvloader" "$NDRVLOADER" || \
    die "Bundled driver loader differs from the tracked firmware"
  cmp -s "$ROOT_DIR/THIRD_PARTY_NOTICES.md" "$NOTICES_BUNDLE" || \
    die "Bundled ThirdPartyNotices.md differs from the tracked notices"
  if [ -f "$ROOT_DIR/ati/host_display.c" ]; then
    RESIZE_DEVICES="$("$PPC_QEMU" -device help 2>&1)"
    grep -q 'classicmac-display-control' <<< "$RESIZE_DEVICES" || \
      die "Bundled Power Mac QEMU lacks the ATI display controller"
    ATI_RESIZE_HELP="$("$PPC_QEMU" -device ati-vga,help 2>&1)"
    grep -q 'host-resize-request' <<< "$ATI_RESIZE_HELP" || \
      die "Bundled ATI device lacks host resize requests"
  fi
fi

VERSION="$(plutil -extract CFBundleShortVersionString raw "$PLIST")"
BUILD="$(plutil -extract CFBundleVersion raw "$PLIST")"
VERSION_MAJOR="${VERSION%%.*}"
VERSION_REMAINDER="${VERSION#*.}"
VERSION_MINOR="${VERSION_REMAINDER%%.*}"
case "$VERSION_MAJOR" in
  ''|*[!0-9]*) die "Unexpected version $VERSION" ;;
esac
if [ "$SKIP_REPO_FRESHNESS" != "1" ] && [ -f "$ROOT_DIR/VERSION" ]; then
  REPO_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")"
  [ "$VERSION" = "$REPO_VERSION" ] || \
    die "Bundled version $VERSION does not match VERSION file ($REPO_VERSION)"
fi

[ -z "$EXPECTED_VERSION" ] || [ "$VERSION" = "$EXPECTED_VERSION" ] || \
  die "Expected version $EXPECTED_VERSION, found $VERSION"
[ -z "$EXPECTED_BUILD" ] || [ "$BUILD" = "$EXPECTED_BUILD" ] || \
  die "Expected build $EXPECTED_BUILD, found $BUILD"

HELPER_BUNDLES=("$QUADRA_HELPER" "$PPC_HELPER" "$COPLAND_HELPER")
IS_47_OR_NEWER=0
if [ "$VERSION_MAJOR" -gt 4 ] || { [ "$VERSION_MAJOR" -eq 4 ] && [ "$VERSION_MINOR" -ge 7 ]; }; then
  IS_47_OR_NEWER=1
  [ -e "$OSX_HELPER/Contents/Info.plist" ] || die "Required OS X helper Info.plist is missing: $OSX_HELPER"
  HELPER_BUNDLES+=("$OSX_HELPER")
fi

for helper in "${HELPER_BUNDLES[@]}"; do
  HELPER_VERSION="$(plutil -extract CFBundleShortVersionString raw "$helper/Contents/Info.plist")"
  HELPER_BUILD="$(plutil -extract CFBundleVersion raw "$helper/Contents/Info.plist")"
  [ "$HELPER_VERSION" = "$VERSION" ] || \
    die "$(basename "$helper") version $HELPER_VERSION does not match app version $VERSION"
  [ "$HELPER_BUILD" = "$BUILD" ] || \
    die "$(basename "$helper") build $HELPER_BUILD does not match app build $BUILD"
done

if [ "$IS_47_OR_NEWER" -eq 1 ]; then
  log "Checking ThirdPartyNotices.md version parity"
  grep -Fq "ClassicMac $VERSION" "$NOTICES_BUNDLE" || \
    die "Bundled ThirdPartyNotices.md does not reference ClassicMac $VERSION"
  grep -Fq "tree/v$VERSION" "$NOTICES_BUNDLE" || \
    die "Bundled ThirdPartyNotices.md does not reference tag v$VERSION"

  log "Checking the Mac OS X engine and flash persistence"
  OSX_FIRMWARE="$APP/Contents/Resources/qemu-osx"
  [ -x "$OSX_QEMU" ] || die "Mac OS X engine is missing"
  [ -s "$OSX_FIRMWARE/keymaps/en-us" ] || die "Mac OS X keymaps are missing"
  "$OSX_QEMU" --version | grep -q '^QEMU emulator version 10\.0\.13' || die "Wrong OS X engine version"
  for fw in openbios-ppc-osx macosx-flash-seed.img qemu_vga.ndrv; do
    [ -s "$OSX_FIRMWARE/$fw" ] || die "Bundled Mac OS X firmware is missing or empty: $fw"
    if [ "$SKIP_REPO_FRESHNESS" != "1" ]; then
      cmp -s "$OSX_FIRMWARE/$fw" "$ROOT_DIR/osx/firmware/$fw" || die "Bundled Mac OS X firmware differs: $fw"
    fi
  done
  python3 "$ROOT_DIR/osx/tests/qtest_macosx_flash.py" --qemu "$OSX_QEMU" \
    --firmware "$OSX_FIRMWARE" --seed-file "$OSX_FIRMWARE/macosx-flash-seed.img"
fi

log "Verifying Developer ID signatures and hardened runtime across app and all 4 helpers"
codesign --verify --deep --strict --verbose=2 "$APP"
SIGNED_ITEMS=("$APP" "$MAIN_BIN" "$QUADRA_QEMU_IMG" "${HELPER_BUNDLES[@]}")
for signed_item in "${SIGNED_ITEMS[@]}"; do
  SIGNING_INFO="$(codesign -dvvv "$signed_item" 2>&1)"
  printf '%s\n' "$SIGNING_INFO" | grep -q \
    '^Authority=Developer ID Application:' || \
    die "Developer ID Application signature missing from $signed_item"
  printf '%s\n' "$SIGNING_INFO" | grep -q '^TeamIdentifier=' || \
    die "Signing team identifier missing from $signed_item"
  printf '%s\n' "$SIGNING_INFO" | grep -q 'flags=.*runtime' || \
    die "Hardened runtime is missing from $signed_item"
done

if [ "$VERIFY_NOTARIZATION" -eq 1 ]; then
  log "Validating the stapled app ticket and executable Gatekeeper policy"
  xcrun stapler validate "$APP"
  spctl --assess --type execute --verbose=2 "$APP"
fi

log "Verifying arm64 binaries, display cards, and RetroBoot engine properties"
ARM64_BINS=("$MAIN_BIN" "$PPC_QEMU" "$QUADRA_QEMU" "$QUADRA_QEMU_IMG" "$COPLAND_ENGINE")
if [ "$IS_47_OR_NEWER" -eq 1 ]; then
  ARM64_BINS+=("$OSX_QEMU")
fi
for bin in "${ARM64_BINS[@]}"; do
  file "$bin" | grep -q 'arm64' || die "$(basename "$bin") is not arm64: $bin"
done

DEVICE_HELP="$("$PPC_QEMU" -device VGA,help 2>&1)"
for property in untracked-vram packed-lowbpp hardware-cursor host-resize classicmac-boot-handoff; do
  printf '%s\n' "$DEVICE_HELP" | grep -q "$property" || \
    die "Bundled Power Mac QEMU lacks VGA.$property"
done
if [ "$IS_47_OR_NEWER" -eq 1 ]; then
  MACIO_IDE_HELP="$("$PPC_QEMU" -device macio-ide,help 2>&1)"
  printf '%s\n' "$MACIO_IDE_HELP" | grep -q 'pio-status-drain' || \
    die "Bundled Power Mac QEMU lacks macio-ide.pio-status-drain"
  OHCI_HELP="$("$PPC_QEMU" -device pci-ohci,help 2>&1)"
  printf '%s\n' "$OHCI_HELP" | grep -q 'idle-frames' || \
    die "Bundled Power Mac QEMU lacks pci-ohci.idle-frames"
  USB_MOUSE_HELP="$("$PPC_QEMU" -device usb-mouse,help 2>&1)"
  printf '%s\n' "$USB_MOUSE_HELP" | grep -q 'wheel-per-report' || \
    die "Bundled Power Mac QEMU lacks usb-mouse.wheel-per-report"
fi
if [ "$VERSION_MAJOR" -ge 4 ]; then
  if printf '%s\n' "$DEVICE_HELP" | grep "gxmetal" >/dev/null; then
    die "Bundled Power Mac QEMU still has the removed GXMetal transport"
  fi
  if LC_ALL=C grep -a -F "GXMetal" "$TOOLS_CD" >/dev/null; then
    die "Bundled Tools CD still carries the removed GXMetal folder"
  fi
  if strings "$PPC_NDRV" | grep -i gxmetal >/dev/null; then
    die "Bundled Power Mac video driver still has the removed GXMetal handoff"
  fi
  log "Verifying included Iconfactory artwork and attribution"
  if [ "$SKIP_REPO_FRESHNESS" != "1" ]; then
    diff -r "$ROOT_DIR/Resources/Iconfactory" "$APP/Contents/Resources/Iconfactory" || \
      die "Bundled Iconfactory artwork or attribution differs from the release source"
  else
    [ -d "$APP/Contents/Resources/Iconfactory" ] || die "Bundled Iconfactory directory is missing"
  fi
  if [ "$VERSION_MAJOR" -gt 4 ] || { [ "$VERSION_MAJOR" -eq 4 ] && [ "$VERSION_MINOR" -ge 2 ]; }; then
    log "Verifying original Mac OS icon attribution and provenance"
    if [ "$SKIP_REPO_FRESHNESS" != "1" ]; then
      cmp "$ROOT_DIR/platinum/macos-icons/README.txt" "$APP/Contents/Resources/Licenses/MacOS-icons.txt" || \
        die "Mac OS icon attribution is missing or stale"
      cmp "$ROOT_DIR/platinum/macos-icons/manifest.json" "$APP/Contents/Resources/Licenses/MacOS-icon-provenance.json" || \
        die "Mac OS icon provenance is missing or stale"
    else
      [ -s "$APP/Contents/Resources/Licenses/MacOS-icons.txt" ] || \
        die "Mac OS icon attribution is missing"
      [ -s "$APP/Contents/Resources/Licenses/MacOS-icon-provenance.json" ] || \
        die "Mac OS icon provenance is missing"
    fi
  fi
  ATI_HELP="$("$PPC_QEMU" -device ati-vga,help 2>&1)"
  for property in model x-accel-stats x-gl-renderer xres yres refresh_rate; do
    printf '%s\n' "$ATI_HELP" | grep -q "$property" || \
      die "Bundled Power Mac QEMU lacks ati-vga.$property"
  done
  strings "$APP/Contents/Resources/qemu/pc-bios/openbios-ppc" | \
    grep -F 'ATY,Rage6p' >/dev/null || \
    die "Bundled OpenBIOS lacks the ATI Radeon (ATY,Rage6p) node"
  if otool -L "$PPC_QEMU" | grep -E '/(OpenGL|Metal)\.framework/' >/dev/null; then
    die "Power Mac QEMU must load OpenGL at run time, not link it"
  fi
fi
for qemu in "$PPC_QEMU" "$QUADRA_QEMU"; do
  DISPLAY_HELP="$("$qemu" -display help 2>&1 || true)"
  printf '%s\n' "$DISPLAY_HELP" | grep -qx 'cocoa' || \
    die "Bundled $(basename "$qemu") lacks the native Cocoa display"
  if [ "$VERSION_MAJOR" -ge 4 ]; then
    # 4.x draws the Mac's screen in ClassicMac's own window.
    printf '%s\n' "$DISPLAY_HELP" | grep -qx 'classicmac' || \
      die "Bundled $(basename "$qemu") lacks the ClassicMac window display (-display classicmac)"
  fi

  VNC_HELP="$("$qemu" -vnc help 2>&1 || true)"
  printf '%s\n' "$VNC_HELP" | grep -q 'vnc options' || \
    die "Bundled $(basename "$qemu") lacks the optional VNC display"
  printf '%s\n' "$VNC_HELP" | grep -q 'websocket=' || \
    die "Bundled $(basename "$qemu") lacks VNC-over-WebSocket support"

  QEMU_STRINGS="$(strings "$qemu")"
  for option in swap-opt-cmd right-click-ctrl scroll-keys; do
    grep -Fq "$option" <<< "$QEMU_STRINGS" || \
      die "Bundled $(basename "$qemu") lacks Cocoa option $option"
  done
  if [ "$VERSION_MAJOR" -ge 3 ]; then
    for marker in classicmac-media com.classicmac.paste-text com.classicmac.media; do
      grep -Fq "$marker" <<< "$QEMU_STRINGS" || \
        die "Bundled $(basename "$qemu") lacks ClassicMac 3 media/text bridge $marker"
    done
  fi
done
if [ "$VERSION_MAJOR" -ge 3 ]; then
  if [ "$VERSION_MAJOR" -eq 3 ]; then
    strings "$PPC_QEMU" | grep -F 'gxmetal-status' >/dev/null || \
      die "Bundled Power Mac QEMU lacks live GXMetal status"
  fi
  for action in /actions/paste-text /actions/media; do
    grep -Fq "$action" "$APP/Contents/Resources/Browser/viewer.js" || \
      die "Bundled browser controls lack $action"
  done
  log "Checking every packaged binary and runtime library supports macOS 15 with no Homebrew refs"
  python3 - "$APP" <<'PY'
import json
from pathlib import Path
import re
import subprocess
import sys

app = Path(sys.argv[1])
licenses_dir = app / "Contents/Resources/Licenses"

def check_macho(binary: Path):
    info = subprocess.check_output(["otool", "-l", str(binary)], text=True)
    minimums = re.findall(r"\bminos ([0-9.]+)", info)
    if not minimums or any(tuple(map(int, (v + ".0.0").split(".")[:3])) > (15, 0, 0) for v in minimums):
        sys.exit(f"Binary needs newer than macOS 15: {binary}")
    deps = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    if "/opt/homebrew" in deps or "@@HOMEBREW" in deps or "/usr/local" in deps:
        sys.exit(f"Unbundled package-manager dependency: {binary}")

def has_notice(formula: str) -> bool:
    stem = formula.split("@", 1)[0].lower()
    if stem == "glib" and (licenses_dir / "LGPL-2.1.txt").is_file() and (licenses_dir / "glib.txt").is_file():
        return True
    for path in licenses_dir.iterdir():
        if path.is_file() and path.stat().st_size > 0:
            name = path.name.lower()
            if name.startswith(stem + ".") or name.startswith(stem + "-") or name == stem:
                return True
    return False

check_macho(app / "Contents/MacOS/ClassicMac")

for helper in sorted((app / "Contents/Helpers").glob("*.app")):
    frameworks = helper / "Contents/Frameworks"
    manifest_path = helper / "Contents/Resources/release-libraries.json"
    if not manifest_path.is_file():
        sys.exit(f"Missing runtime library provenance: {helper.name}")
    dylibs = list(frameworks.glob("*.dylib"))
    if dylibs:
        manifest = json.loads(manifest_path.read_text())
        by_name = {entry["name"]: entry["formula"] for entry in manifest.get("libraries", [])}
        for dylib in dylibs:
            formula = by_name.get(dylib.name)
            if not formula:
                sys.exit(f"Unrecorded Frameworks dylib {dylib.name} in {helper.name}")
            if not has_notice(formula):
                sys.exit(f"Missing license notice in Licenses/ for {dylib.name} ({formula})")
    for binary in dylibs + list((helper / "Contents/MacOS").iterdir()):
        check_macho(binary)

print("Main app and all helper binaries/libraries target macOS 15 or earlier with no Homebrew refs.")
PY
fi
grep -q 'pseudoEncodingQEMUPointerTypeChange' "$BROWSER_RFB" || \
  die "Bundled browser client lacks QEMU relative-pointer support"
grep -q 'Math.floor(fit)' "$BROWSER_SCALE" || \
  die "Bundled browser client lacks whole-number display scaling"

log "Release verification passed"
printf '    Version:       %s (%s)\n' "$VERSION" "$BUILD"
printf '    Tools CD SHA:  %s\n' "$(shasum -a 256 "$TOOLS_CD" | awk '{ print $1 }')"
printf '    Power NDRV SHA: %s\n' "$(shasum -a 256 "$PPC_NDRV" | awk '{ print $1 }')"
if [ -f "$TARGET" ]; then
  printf '    Target SHA:    %s\n' "$(shasum -a 256 "$TARGET" | awk '{ print $1 }')"
else
  printf '    Power Mac SHA: %s\n' "$(shasum -a 256 "$PPC_QEMU" | awk '{ print $1 }')"
fi
