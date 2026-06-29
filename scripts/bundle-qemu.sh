#!/usr/bin/env bash
#
# bundle-qemu.sh - Build the SwiftUI app and assemble a self-contained
# ClassicMac.app for Apple Silicon: the app binary, the custom qemu-system-m68k
# and qemu-img, the enhanced framebuffer firmware, the Quadra 800 ROM, and all
# required dynamic libraries (relocated with dylibbundler), code-signed so QEMU's
# JIT runs on Apple Silicon.
#
# Idempotent: the app bundle is rebuilt from scratch on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_SRC_DIR="$ROOT_DIR/app"
QEMU_BUILD_DIR="$ROOT_DIR/vendor/qemu/build"
QEMU_PCBIOS_DIR="$ROOT_DIR/vendor/qemu/pc-bios"
ROM_SRC="$ROOT_DIR/Resources/Quadra800.rom"
DECLROM_SRC="$ROOT_DIR/shared/declrom"
PRAMSEED_SRC="$ROOT_DIR/shared/pram-seed.img"
ENTITLEMENTS="$ROOT_DIR/scripts/qemu.entitlements"

DIST_DIR="$ROOT_DIR/dist"
APP="$DIST_DIR/ClassicMac.app"
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RES_DIR="$CONTENTS/Resources"
QEMU_DEST="$RES_DIR/qemu"
PCBIOS_DEST="$QEMU_DEST/pc-bios"
FRAMEWORKS_DIR="$RES_DIR/Frameworks"

APP_VERSION="${APP_VERSION:-1.0}"
BUNDLE_ID="com.classicmac.emulator"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 0. Preconditions
# ---------------------------------------------------------------------------
[ -x "$QEMU_BUILD_DIR/qemu-system-m68k" ] || die "qemu-system-m68k not found. Run scripts/build-qemu.sh first."
[ -x "$QEMU_BUILD_DIR/qemu-img" ] || die "qemu-img not found. Run scripts/build-qemu.sh first."
[ -f "$QEMU_PCBIOS_DIR/mac_qfb.rom" ] || die "mac_qfb.rom firmware not found. Run scripts/build-qemu.sh first."
[ -f "$ROM_SRC" ] || die "Quadra800.rom not found in Resources/."
[ -f "$DECLROM_SRC" ] || die "shared/declrom (classicvirtio declaration ROM) not found."
[ -f "$PRAMSEED_SRC" ] || die "shared/pram-seed.img (PRAM seed) not found."
command -v dylibbundler >/dev/null 2>&1 || die "dylibbundler is required (brew install dylibbundler)."

# ---------------------------------------------------------------------------
# 1. Build the SwiftUI app (release)
# ---------------------------------------------------------------------------
log "Building ClassicMac app (release)"
( cd "$APP_SRC_DIR" && swift build -c release )
APP_BIN="$APP_SRC_DIR/.build/release/ClassicMac"
[ -x "$APP_BIN" ] || die "Swift build did not produce the ClassicMac executable."

# ---------------------------------------------------------------------------
# 2. Assemble the .app skeleton (clean each run)
# ---------------------------------------------------------------------------
log "Assembling $APP"
rm -rf "$APP"
mkdir -p "$MACOS_DIR" "$QEMU_DEST" "$PCBIOS_DEST" "$FRAMEWORKS_DIR"

cp "$APP_BIN" "$MACOS_DIR/ClassicMac"
cp "$QEMU_BUILD_DIR/qemu-system-m68k" "$QEMU_DEST/"
cp "$QEMU_BUILD_DIR/qemu-img" "$QEMU_DEST/"
cp "$ROM_SRC" "$RES_DIR/Quadra800.rom"
cp "$QEMU_PCBIOS_DIR/mac_qfb.rom" "$PCBIOS_DEST/"
cp "$DECLROM_SRC" "$RES_DIR/declrom"
cp "$PRAMSEED_SRC" "$RES_DIR/pram-seed.img"

# ---------------------------------------------------------------------------
# 3. Info.plist
# ---------------------------------------------------------------------------
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>ClassicMac</string>
	<key>CFBundleDisplayName</key>
	<string>ClassicMac</string>
	<key>CFBundleIdentifier</key>
	<string>$BUNDLE_ID</string>
	<key>CFBundleVersion</key>
	<string>$APP_VERSION</string>
	<key>CFBundleShortVersionString</key>
	<string>$APP_VERSION</string>
	<key>CFBundleExecutable</key>
	<string>ClassicMac</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>LSMinimumSystemVersion</key>
	<string>13.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.developer-tools</string>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>
PLIST

printf 'APPL????' > "$CONTENTS/PkgInfo"

# ---------------------------------------------------------------------------
# 4. Relocate dynamic libraries into the bundle
# ---------------------------------------------------------------------------
log "Bundling dynamic libraries with dylibbundler"
# @executable_path for the qemu binaries is Contents/Resources/qemu, so
# ../Frameworks resolves to Contents/Resources/Frameworks.
dylibbundler -of -b \
  -x "$QEMU_DEST/qemu-system-m68k" \
  -x "$QEMU_DEST/qemu-img" \
  -d "$FRAMEWORKS_DIR" \
  -p "@executable_path/../Frameworks" \
  -s "$(brew --prefix)/lib"

# dylibbundler rewrites every pre-existing LC_RPATH to the same value, which
# leaves duplicate LC_RPATH entries that modern dyld refuses to load. Collapse
# them down to a single @executable_path/../Frameworks rpath.
dedupe_rpaths() {
  local bin="$1"
  while otool -l "$bin" | grep -q "LC_RPATH"; do
    local current
    current="$(otool -l "$bin" | awk '/LC_RPATH/{getline; getline; print $2; exit}')"
    install_name_tool -delete_rpath "$current" "$bin" 2>/dev/null || break
  done
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin"
}

log "Collapsing duplicate rpaths"
dedupe_rpaths "$QEMU_DEST/qemu-system-m68k"
dedupe_rpaths "$QEMU_DEST/qemu-img"

# ---------------------------------------------------------------------------
# 5. Code signing (ad-hoc). Sign inner items first, then outward.
# ---------------------------------------------------------------------------
log "Code signing (ad-hoc)"
find "$FRAMEWORKS_DIR" -name '*.dylib' -print0 | while IFS= read -r -d '' lib; do
  codesign --force --sign - --timestamp=none "$lib"
done

# qemu-system-m68k needs the JIT entitlements; qemu-img does not but signing it
# keeps the bundle valid.
codesign --force --sign - --timestamp=none \
  --options runtime --entitlements "$ENTITLEMENTS" \
  "$QEMU_DEST/qemu-system-m68k"
codesign --force --sign - --timestamp=none "$QEMU_DEST/qemu-img"

codesign --force --sign - --timestamp=none "$MACOS_DIR/ClassicMac"

# Sign the whole bundle last.
codesign --force --sign - --timestamp=none "$APP"

# ---------------------------------------------------------------------------
# 6. Verify
# ---------------------------------------------------------------------------
log "Verifying bundle signature"
codesign --verify --deep --strict --verbose=2 "$APP" || die "Bundle failed signature verification."

log "Done. Built: $APP"
log "Launch with: open \"$APP\""
