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
#   scripts/verify-release.sh dist/ClassicMac.app 1.9.0 1.9.0
#   scripts/verify-release.sh dist/ClassicMac.dmg 1.9.0 1.9.0

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-$ROOT_DIR/dist/ClassicMac.dmg}"
EXPECTED_VERSION="${2:-${APP_VERSION:-}}"
EXPECTED_BUILD="${3:-${APP_BUILD_VERSION:-}}"
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

for tool in cmp codesign file plutil shasum spctl xcrun; do
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
PPC_HELPER="$APP/Contents/Helpers/Power Mac G4.app"
PPC_QEMU="$PPC_HELPER/Contents/MacOS/qemu-system-ppc"
TOOLS_CD="$APP/Contents/Resources/ClassicMacTools.iso"
VNC_KEYMAP="$APP/Contents/Resources/qemu/pc-bios/keymaps/en-us"

for required in "$PLIST" "$PPC_HELPER/Contents/Info.plist" \
  "$PPC_QEMU" "$TOOLS_CD" "$VNC_KEYMAP"; do
  [ -e "$required" ] || die "Required release component is missing: $required"
done
[ -s "$TOOLS_CD" ] || die "Bundled ClassicMac Tools CD is empty"
if [ -f "$ROOT_DIR/dist/ClassicMacTools.iso" ]; then
  cmp -s "$ROOT_DIR/dist/ClassicMacTools.iso" "$TOOLS_CD" || \
    die "Bundled Tools CD differs from the freshly built dist image"
fi

VERSION="$(plutil -extract CFBundleShortVersionString raw "$PLIST")"
BUILD="$(plutil -extract CFBundleVersion raw "$PLIST")"
HELPER_VERSION="$(plutil -extract CFBundleShortVersionString raw \
  "$PPC_HELPER/Contents/Info.plist")"
HELPER_BUILD="$(plutil -extract CFBundleVersion raw \
  "$PPC_HELPER/Contents/Info.plist")"

[ -z "$EXPECTED_VERSION" ] || [ "$VERSION" = "$EXPECTED_VERSION" ] || \
  die "Expected version $EXPECTED_VERSION, found $VERSION"
[ -z "$EXPECTED_BUILD" ] || [ "$BUILD" = "$EXPECTED_BUILD" ] || \
  die "Expected build $EXPECTED_BUILD, found $BUILD"
[ "$HELPER_VERSION" = "$VERSION" ] || \
  die "Power Mac helper version $HELPER_VERSION does not match app version $VERSION"
[ "$HELPER_BUILD" = "$BUILD" ] || \
  die "Power Mac helper build $HELPER_BUILD does not match app build $BUILD"

log "Verifying Developer ID signatures and hardened runtime"
codesign --verify --deep --strict --verbose=2 "$APP"
for signed_item in "$APP" "$PPC_HELPER"; do
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

log "Verifying the bundled GXMetal-capable Power Mac executable"
file "$PPC_QEMU" | grep -q 'arm64' || die "Power Mac QEMU is not arm64"
DEVICE_HELP="$("$PPC_QEMU" -device VGA,help 2>&1)"
for property in gxmetal untracked-vram packed-lowbpp hardware-cursor host-resize; do
  printf '%s\n' "$DEVICE_HELP" | grep -q "$property" || \
    die "Bundled Power Mac QEMU lacks VGA.$property"
done
VNC_HELP="$("$PPC_QEMU" -vnc help 2>&1 || true)"
printf '%s\n' "$VNC_HELP" | grep -q 'vnc options' || \
  die "Bundled Power Mac QEMU lacks the headless VNC display"
if otool -L "$PPC_QEMU" | grep -Eq '/opt/homebrew|/usr/local'; then
  die "Bundled Power Mac QEMU still references a package-manager library"
fi

log "Release verification passed"
printf '    Version:       %s (%s)\n' "$VERSION" "$BUILD"
printf '    Tools CD SHA:  %s\n' "$(shasum -a 256 "$TOOLS_CD" | awk '{ print $1 }')"
if [ -f "$TARGET" ]; then
  printf '    Target SHA:    %s\n' "$(shasum -a 256 "$TARGET" | awk '{ print $1 }')"
else
  printf '    Power Mac SHA: %s\n' "$(shasum -a 256 "$PPC_QEMU" | awk '{ print $1 }')"
fi
