#!/usr/bin/env bash
#
# bundle-qemu.sh - Build the SwiftUI app and assemble a self-contained
# ClassicMac.app for Apple Silicon: the app binary, the custom qemu-system-m68k,
# qemu-system-ppc and qemu-img, the enhanced framebuffer firmware, the Quadra
# 800 ROM, the OpenBIOS PPC firmware, and all required dynamic libraries
# (relocated with dylibbundler), code-signed so QEMU's JIT runs on Apple
# Silicon.
#
# Idempotent: the app bundle is rebuilt from scratch on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_SRC_DIR="$ROOT_DIR/app"
QEMU_BUILD_DIR="$ROOT_DIR/vendor/qemu/build"
QEMU_PCBIOS_DIR="$ROOT_DIR/vendor/qemu/pc-bios"
ROM_SRC="$ROOT_DIR/Resources/Quadra800.rom"
ICON_PNG="$ROOT_DIR/Resources/AppIcon.png"
ICON_DOC="$ROOT_DIR/Resources/AppIcon.icon"
DECLROM_SRC="$ROOT_DIR/shared/declrom"
NDRVLOADER_SRC="$ROOT_DIR/shared/ndrvloader"
PRAMSEED_SRC="$ROOT_DIR/shared/pram-seed.img"
ENTITLEMENTS="$ROOT_DIR/scripts/qemu.entitlements"
THIRD_PARTY_NOTICES="$ROOT_DIR/THIRD_PARTY_NOTICES.md"

DIST_DIR="$ROOT_DIR/dist"
APP="$DIST_DIR/ClassicMac.app"
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RES_DIR="$CONTENTS/Resources"
QEMU_DEST="$RES_DIR/qemu"
PCBIOS_DEST="$QEMU_DEST/pc-bios"
HELPERS_DIR="$CONTENTS/Helpers"
# The emulator binaries live inside per-family helper app bundles so the
# running machine appears in the Dock and app switcher as "Quadra 800" /
# "Power Mac G4" with a proper icon, not as a bare qemu-system executable.
QUADRA_APP="$HELPERS_DIR/Quadra 800.app"
PPC_APP="$HELPERS_DIR/Power Mac G4.app"

APP_VERSION="${APP_VERSION:-1.2.1}"
BUNDLE_ID="com.classicmac.emulator"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 0. Preconditions
# ---------------------------------------------------------------------------
[ -x "$QEMU_BUILD_DIR/qemu-system-m68k" ] || die "qemu-system-m68k not found. Run scripts/build-qemu.sh first."
[ -x "$QEMU_BUILD_DIR/qemu-system-ppc" ] || die "qemu-system-ppc not found. Run scripts/build-qemu.sh first."
[ -x "$QEMU_BUILD_DIR/qemu-img" ] || die "qemu-img not found. Run scripts/build-qemu.sh first."
# Firmware the emulated machines load at runtime from the -L directory:
#   mac_qfb.rom        - Quadra enhanced framebuffer declaration ROM
#   openbios-ppc       - Power Mac boot firmware (screamer-aware build)
#   vgabios-stdvga.bin - VGA option ROM for the mac99 std VGA display
#   qemu_vga.ndrv      - Mac OS video driver OpenBIOS hands to the guest
#                        (ClassicMac build from ppcvid/ with live host-window
#                        resizing; installed by build-qemu.sh)
PCBIOS_FILES=(mac_qfb.rom openbios-ppc vgabios-stdvga.bin qemu_vga.ndrv)
for fw in "${PCBIOS_FILES[@]}"; do
  [ -f "$QEMU_PCBIOS_DIR/$fw" ] || die "$fw firmware not found. Run scripts/build-qemu.sh first."
done
[ -f "$ROM_SRC" ] || die "Quadra800.rom not found in Resources/."
[ -f "$ICON_PNG" ] || die "AppIcon.png not found in Resources/."
[ -f "$ICON_DOC/icon.json" ] || die "AppIcon.icon (Icon Composer document) not found in Resources/."
[ -f "$ROOT_DIR/Resources/MachineIcon.icns" ] || die "MachineIcon.icns not found in Resources/."
[ -f "$DECLROM_SRC" ] || die "shared/declrom (classicvirtio declaration ROM) not found."
[ -f "$NDRVLOADER_SRC" ] || die "shared/ndrvloader (classicvirtio PPC driver loader) not found."
[ -f "$PRAMSEED_SRC" ] || die "shared/pram-seed.img (PRAM seed) not found."
[ -f "$THIRD_PARTY_NOTICES" ] || die "THIRD_PARTY_NOTICES.md not found."
[ -f "$ROOT_DIR/vendor/qemu/LICENSE" ] || die "QEMU LICENSE not found. Run scripts/build-qemu.sh first."
[ -f "$ROOT_DIR/vendor/qemu/COPYING" ] || die "QEMU GPL license not found. Run scripts/build-qemu.sh first."
[ -f "$ROOT_DIR/vendor/qemu/COPYING.LIB" ] || die "QEMU LGPL license not found. Run scripts/build-qemu.sh first."
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
mkdir -p "$MACOS_DIR" "$QEMU_DEST" "$PCBIOS_DEST"
mkdir -p "$QUADRA_APP/Contents/MacOS" "$QUADRA_APP/Contents/Frameworks" "$QUADRA_APP/Contents/Resources"
mkdir -p "$PPC_APP/Contents/MacOS" "$PPC_APP/Contents/Frameworks" "$PPC_APP/Contents/Resources"

cp "$APP_BIN" "$MACOS_DIR/ClassicMac"
cp "$QEMU_BUILD_DIR/qemu-system-m68k" "$QUADRA_APP/Contents/MacOS/"
cp "$QEMU_BUILD_DIR/qemu-img" "$QUADRA_APP/Contents/MacOS/"
cp "$QEMU_BUILD_DIR/qemu-system-ppc" "$PPC_APP/Contents/MacOS/"
cp "$ROOT_DIR/Resources/MachineIcon.icns" "$QUADRA_APP/Contents/Resources/MachineIcon.icns"
cp "$ROOT_DIR/Resources/MachineIcon.icns" "$PPC_APP/Contents/Resources/MachineIcon.icns"
cp "$ROM_SRC" "$RES_DIR/Quadra800.rom"

# App icon, two generations of it:
#
# 1) Legacy AppIcon.icns for macOS 15 and earlier - always derived fresh from
#    the full-bleed master PNG, whose artwork must cover the entire 1024x1024
#    canvas (its own rounded-rect shape included). A pre-shrunken icns once
#    crept in here and macOS responded by drawing the undersized art on its
#    own synthesized backdrop - the "growing grey border" - so the icns is
#    regenerated from the master on every bundle instead of being a second,
#    driftable copy in the repo.
log "Generating AppIcon.icns from Resources/AppIcon.png"
ICONSET_DIR="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$ICONSET_DIR"
for spec in \
  "16 icon_16x16" \
  "32 icon_16x16@2x" \
  "32 icon_32x32" \
  "64 icon_32x32@2x" \
  "128 icon_128x128" \
  "256 icon_128x128@2x" \
  "256 icon_256x256" \
  "512 icon_256x256@2x" \
  "512 icon_512x512" \
  "1024 icon_512x512@2x"; do
  set -- $spec
  sips -z "$1" "$1" "$ICON_PNG" --out "$ICONSET_DIR/$2.png" >/dev/null || die "Failed to scale AppIcon.png to $1x$1"
done
iconutil --convert icns -o "$RES_DIR/AppIcon.icns" "$ICONSET_DIR" || die "iconutil failed to build AppIcon.icns"
rm -rf "$(dirname "$ICONSET_DIR")"

# 2) Liquid Glass icon for macOS 26 (Tahoe) and later, compiled from the Icon
#    Composer document Resources/AppIcon.icon into Assets.car and referenced
#    by CFBundleIconName. Without this, macOS 26 refuses the legacy icns as-is
#    and renders it shrunken on a synthesized squircle backdrop ("icon jail"),
#    no matter how the icns artwork is shaped. Compiling needs actool from a
#    full Xcode 26 install; when it is unavailable the build still succeeds
#    with the legacy icon only (and the jailed look on Tahoe).
ICON_NAME_PLIST=""
ACTOOL_OUT="$(mktemp -d)"
if xcrun actool "$ICON_DOC" --compile "$ACTOOL_OUT" \
     --output-format human-readable-text \
     --output-partial-info-plist "$ACTOOL_OUT/partial.plist" \
     --app-icon AppIcon --include-all-app-icons \
     --enable-on-demand-resources NO \
     --development-region en \
     --target-device mac \
     --minimum-deployment-target 26.0 \
     --platform macosx >/dev/null 2>&1 && [ -f "$ACTOOL_OUT/Assets.car" ]; then
  log "Compiling Liquid Glass icon (Assets.car) from Resources/AppIcon.icon"
  cp "$ACTOOL_OUT/Assets.car" "$RES_DIR/Assets.car"
  ICON_NAME_PLIST="	<key>CFBundleIconName</key>
	<string>AppIcon</string>"
else
  log "WARNING: actool unavailable or failed; skipping the macOS 26 Liquid Glass icon (app will show the legacy icon on a synthesized backdrop). Install full Xcode 26 to fix."
fi
rm -rf "$ACTOOL_OUT"
for fw in "${PCBIOS_FILES[@]}"; do
  cp "$QEMU_PCBIOS_DIR/$fw" "$PCBIOS_DEST/"
done
cp "$DECLROM_SRC" "$RES_DIR/declrom"
cp "$NDRVLOADER_SRC" "$RES_DIR/ndrvloader"
cp "$PRAMSEED_SRC" "$RES_DIR/pram-seed.img"

# Third-party notices and license texts for QEMU, its firmware, and every
# Homebrew library copied into the self-contained helper apps below.
LICENSES_DIR="$RES_DIR/Licenses"
mkdir -p "$LICENSES_DIR"
cp "$THIRD_PARTY_NOTICES" "$RES_DIR/ThirdPartyNotices.md"
cp "$ROOT_DIR/vendor/qemu/LICENSE" "$LICENSES_DIR/QEMU-LICENSE.txt"
cp "$ROOT_DIR/vendor/qemu/COPYING" "$LICENSES_DIR/GPL-2.0.txt"
cp "$ROOT_DIR/vendor/qemu/COPYING.LIB" "$LICENSES_DIR/LGPL-2.1.txt"

copy_brew_license() {
  local formula="$1" source_name="$2" destination_name="$3"
  local source_path
  source_path="$(brew --prefix "$formula")/$source_name"
  [ -f "$source_path" ] || die "$formula license not found at $source_path"
  cp "$source_path" "$LICENSES_DIR/$destination_name"
}

copy_brew_license pixman COPYING pixman.txt
copy_brew_license libpng LICENSE libpng.txt
copy_brew_license zstd LICENSE zstd.txt
copy_brew_license libslirp LICENSE libslirp.txt
copy_brew_license libslirp COPYRIGHT libslirp-COPYRIGHT.txt
copy_brew_license libusb COPYING libusb.txt
copy_brew_license gettext COPYING gettext.txt
copy_brew_license pcre2 COPYING pcre2.txt

# Guest additions CD (StuffIt Expander, USB Overdrive, Disk Copy, ...).
# Built by scripts/build-guest-cd.sh; optional so a plain QEMU rebuild
# doesn't require network access.
TOOLS_CD_SRC="$ROOT_DIR/dist/ClassicMacTools.iso"
if [ -f "$TOOLS_CD_SRC" ]; then
  cp "$TOOLS_CD_SRC" "$RES_DIR/ClassicMacTools.iso"
else
  log "WARNING: dist/ClassicMacTools.iso not found; app will lack the Tools CD (run scripts/build-guest-cd.sh)"
fi

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
	<key>CFBundleIconFile</key>
	<string>AppIcon</string>
$ICON_NAME_PLIST
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>LSMinimumSystemVersion</key>
	<string>15.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.utilities</string>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
	<key>CFBundleDocumentTypes</key>
	<array>
		<dict>
			<key>CFBundleTypeName</key>
			<string>ClassicMac Machine</string>
			<key>CFBundleTypeRole</key>
			<string>Editor</string>
			<key>LSHandlerRank</key>
			<string>Owner</string>
			<key>LSTypeIsPackage</key>
			<true/>
			<key>LSItemContentTypes</key>
			<array>
				<string>com.classicmac.vm</string>
			</array>
		</dict>
	</array>
	<key>UTExportedTypeDeclarations</key>
	<array>
		<dict>
			<key>UTTypeIdentifier</key>
			<string>com.classicmac.vm</string>
			<key>UTTypeDescription</key>
			<string>ClassicMac Machine</string>
			<key>UTTypeConformsTo</key>
			<array>
				<string>com.apple.package</string>
			</array>
			<key>UTTypeTagSpecification</key>
			<dict>
				<key>public.filename-extension</key>
				<array>
					<string>classic</string>
				</array>
			</dict>
		</dict>
	</array>
</dict>
</plist>
PLIST

printf 'APPL????' > "$CONTENTS/PkgInfo"

# ---------------------------------------------------------------------------
# 3b. Helper app Info.plists
# ---------------------------------------------------------------------------
# Each emulator binary lives in its own minimal .app so macOS shows the
# running machine with a friendly name and icon in the Dock and app switcher.
write_helper_plist() {
  local app="$1" name="$2" bundle_id="$3" executable="$4"
  cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>$name</string>
	<key>CFBundleDisplayName</key>
	<string>$name</string>
	<key>CFBundleIdentifier</key>
	<string>$bundle_id</string>
	<key>CFBundleVersion</key>
	<string>$APP_VERSION</string>
	<key>CFBundleShortVersionString</key>
	<string>$APP_VERSION</string>
	<key>CFBundleExecutable</key>
	<string>$executable</string>
	<key>CFBundleIconFile</key>
	<string>MachineIcon</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>LSMinimumSystemVersion</key>
	<string>15.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>
PLIST
  printf 'APPL????' > "$app/Contents/PkgInfo"
}

write_helper_plist "$QUADRA_APP" "Quadra 800" "com.classicmac.machine.quadra800" "qemu-system-m68k"
write_helper_plist "$PPC_APP" "Power Mac G4" "com.classicmac.machine.powermacg4" "qemu-system-ppc"

# ---------------------------------------------------------------------------
# 4. Relocate dynamic libraries into the helper bundles
# ---------------------------------------------------------------------------
log "Bundling dynamic libraries with dylibbundler"
# @executable_path for each qemu binary is <helper>.app/Contents/MacOS, so
# ../Frameworks resolves to that helper's Contents/Frameworks.
dylibbundler -of -b \
  -x "$QUADRA_APP/Contents/MacOS/qemu-system-m68k" \
  -x "$QUADRA_APP/Contents/MacOS/qemu-img" \
  -d "$QUADRA_APP/Contents/Frameworks" \
  -p "@executable_path/../Frameworks" \
  -s "$(brew --prefix)/lib"

dylibbundler -of -b \
  -x "$PPC_APP/Contents/MacOS/qemu-system-ppc" \
  -d "$PPC_APP/Contents/Frameworks" \
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
dedupe_rpaths "$QUADRA_APP/Contents/MacOS/qemu-system-m68k"
dedupe_rpaths "$QUADRA_APP/Contents/MacOS/qemu-img"
dedupe_rpaths "$PPC_APP/Contents/MacOS/qemu-system-ppc"

# ---------------------------------------------------------------------------
# 5. Code signing. Sign inner items first, then outward.
#
# Uses the Developer ID Application certificate when one is present in the
# keychain (required for notarization / distribution); falls back to ad-hoc
# signing otherwise. Override with SIGN_IDENTITY=<identity or "-">.
# ---------------------------------------------------------------------------
SIGN_IDENTITY="${SIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
  SIGN_IDENTITY="$(security find-identity -v -p codesigning | awk -F'"' '/Developer ID Application/{print $2; exit}')"
fi
if [ -z "$SIGN_IDENTITY" ]; then
  SIGN_IDENTITY="-"
fi

# Developer ID signatures need a secure timestamp for notarization; ad-hoc
# signatures cannot get one.
TIMESTAMP_FLAG="--timestamp"
if [ "$SIGN_IDENTITY" = "-" ]; then
  TIMESTAMP_FLAG="--timestamp=none"
  log "Code signing (ad-hoc)"
else
  log "Code signing (identity: $SIGN_IDENTITY)"
fi

find "$QUADRA_APP/Contents/Frameworks" "$PPC_APP/Contents/Frameworks" -name '*.dylib' -print0 | while IFS= read -r -d '' lib; do
  codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" "$lib"
done

# qemu-img is not the helper bundle's main executable, so it needs its own
# signature (no JIT entitlements required). All executables get the hardened
# runtime, which notarization requires.
codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" \
  --options runtime \
  "$QUADRA_APP/Contents/MacOS/qemu-img"

# Sign the helper apps as bundles, then the outer app. Signing a bundle
# (re-)signs its main executable - the qemu-system binary - so the JIT
# entitlements MUST be supplied here or this pass would strip them, leaving a
# hardened-runtime binary that cannot map its JIT buffer ("allocate ... bytes
# for jit buffer: Invalid argument" at launch).
codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" \
  --options runtime --entitlements "$ENTITLEMENTS" \
  "$QUADRA_APP"
codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" \
  --options runtime --entitlements "$ENTITLEMENTS" \
  "$PPC_APP"

codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" --options runtime "$MACOS_DIR/ClassicMac"

# Sign the whole bundle last.
codesign --force --sign "$SIGN_IDENTITY" "$TIMESTAMP_FLAG" --options runtime "$APP"

# ---------------------------------------------------------------------------
# 6. Verify
# ---------------------------------------------------------------------------
log "Verifying bundle signature"
codesign --verify --deep --strict --verbose=2 "$APP" || die "Bundle failed signature verification."

log "Done. Built: $APP"
log "Launch with: open \"$APP\""
