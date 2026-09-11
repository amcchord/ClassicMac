#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CLASSICMAC_IOS_BUILD_DIR:-$ROOT_DIR/build/ipad}"
UTM_REVISION="8e4de50817e76a83d6840212311627a78dd4f8b2"
UTM_DIR="$BUILD_DIR/UTM-$UTM_REVISION"
SYSROOT_NAME="sysroot-iOS-TCI-arm64"
SYSROOT_ARTIFACT="Sysroot-ios-tci-arm64"
SYSROOT_RUN_ID="30986822409"
MARKETING_VERSION="${CLASSICMAC_MARKETING_VERSION:-1.0.0}"
BUILD_NUMBER="${CLASSICMAC_BUILD_NUMBER:-1}"
ARCHIVE_PATH="${CLASSICMAC_ARCHIVE_PATH:-$BUILD_DIR/ClassicMac-iPad-$BUILD_NUMBER.xcarchive}"
BUILD_LOG="$BUILD_DIR/archive-$BUILD_NUMBER.log"
ICON_SET="$UTM_DIR/Platform/Assets.xcassets/AppIconClassic.appiconset"
SIGNING_MODE="${CLASSICMAC_SIGNING:-none}"

mkdir -p "$BUILD_DIR"

if [[ ! -d "$UTM_DIR/.git" ]]; then
    git clone --filter=blob:none https://github.com/utmapp/UTM.git "$UTM_DIR"
    git -C "$UTM_DIR" checkout --detach "$UTM_REVISION"
fi

if [[ "$(git -C "$UTM_DIR" rev-parse HEAD)" != "$UTM_REVISION" ]]; then
    echo "Refusing to build an unexpected UTM revision in $UTM_DIR" >&2
    exit 1
fi

if [[ ! -f "$UTM_DIR/.classicmac-patch-applied" ]]; then
    git -C "$UTM_DIR" apply --check "$ROOT_DIR/ios/utm-classicmac.patch"
    git -C "$UTM_DIR" apply "$ROOT_DIR/ios/utm-classicmac.patch"
    touch "$UTM_DIR/.classicmac-patch-applied"
fi

if [[ ! -d "$UTM_DIR/$SYSROOT_NAME" ]]; then
    command -v gh >/dev/null || {
        echo "GitHub CLI is required to download the pinned UTM iOS sysroot." >&2
        exit 1
    }
    gh run download "$SYSROOT_RUN_ID" \
        --repo utmapp/UTM \
        --name "$SYSROOT_ARTIFACT" \
        --dir "$UTM_DIR"
    tar -xzf "$UTM_DIR/sysroot.tgz" -C "$UTM_DIR"
fi

if ! xcrun --find metal >/dev/null 2>&1; then
    xcodebuild -downloadComponent MetalToolchain
fi

if [[ ! -d "$ICON_SET" ]]; then
    cp -R "$UTM_DIR/Platform/Assets.xcassets/AppIcon.appiconset" "$ICON_SET"
fi

while IFS= read -r icon; do
    width="$(sips -g pixelWidth "$icon" | awk '/pixelWidth/ { print $2 }')"
    sips -z "$width" "$width" "$ROOT_DIR/Resources/AppIcon.png" --out "$icon" >/dev/null
done < <(find "$ICON_SET" -maxdepth 1 -type f -name '*.png' -print)

signing_args=(CODE_SIGNING_ALLOWED=NO)
if [[ "$SIGNING_MODE" == "automatic" ]]; then
    : "${CLASSICMAC_TEAM_ID:?Set CLASSICMAC_TEAM_ID for automatic signing.}"
    : "${APP_STORE_CONNECT_KEY_ID:?Set APP_STORE_CONNECT_KEY_ID for automatic signing.}"
    : "${APP_STORE_CONNECT_ISSUER_ID:?Set APP_STORE_CONNECT_ISSUER_ID for automatic signing.}"
    : "${APP_STORE_CONNECT_KEY_PATH:?Set APP_STORE_CONNECT_KEY_PATH for automatic signing.}"
    signing_args=(
        CODE_SIGNING_ALLOWED=YES
        CODE_SIGN_STYLE=Automatic
        "DEVELOPMENT_TEAM=$CLASSICMAC_TEAM_ID"
    )
    provisioning_args=(
        -allowProvisioningUpdates
        -authenticationKeyPath "$APP_STORE_CONNECT_KEY_PATH"
        -authenticationKeyID "$APP_STORE_CONNECT_KEY_ID"
        -authenticationKeyIssuerID "$APP_STORE_CONNECT_ISSUER_ID"
    )
elif [[ "$SIGNING_MODE" != "none" ]]; then
    echo "CLASSICMAC_SIGNING must be 'none' or 'automatic'." >&2
    exit 1
fi

if [[ -e "$ARCHIVE_PATH" ]]; then
    mv "$ARCHIVE_PATH" "$ARCHIVE_PATH.previous-$(date +%Y%m%d%H%M%S)"
fi

xcodebuild_args=(
    archive
    -project "$UTM_DIR/UTM.xcodeproj"
    -scheme iOS-SE
    -configuration Release
    -sdk iphoneos
    -arch arm64
    -archivePath "$ARCHIVE_PATH"
)
if [[ "$SIGNING_MODE" == "automatic" ]]; then
    xcodebuild_args+=("${provisioning_args[@]}")
fi
xcodebuild_args+=(
    "${signing_args[@]}"
    PRODUCT_BUNDLE_PREFIX=com.classicmac
    PRODUCT_BUNDLE_IDENTIFIER=com.classicmac.emulator
    MARKETING_VERSION="$MARKETING_VERSION"
    CURRENT_PROJECT_VERSION="$BUILD_NUMBER"
    TARGETED_DEVICE_FAMILY=2
    ASSETCATALOG_COMPILER_APPICON_NAME=AppIconClassic
    "SWIFT_ACTIVE_COMPILATION_CONDITIONS=WITH_QEMU_TCI WITH_SOLO_VM CLASSICMAC"
)

if ! xcodebuild "${xcodebuild_args[@]}" >"$BUILD_LOG" 2>&1; then
    tail -n 120 "$BUILD_LOG" >&2
    exit 1
fi

APP_PATH="$ARCHIVE_PATH/Products/Applications/UTM SE.app"
PLIST="$APP_PATH/Info.plist"

[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$PLIST")" == "com.classicmac.emulator" ]]
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleDisplayName' "$PLIST")" == "ClassicMac" ]]
[[ "$(/usr/libexec/PlistBuddy -c 'Print :UIDeviceFamily:0' "$PLIST")" == "2" ]]
[[ -d "$APP_PATH/Frameworks/qemu-m68k-softmmu.framework" ]]
[[ -d "$APP_PATH/Frameworks/qemu-ppc-softmmu.framework" ]]

for unused in aarch64 i386 ppc64 riscv64 x86_64; do
    [[ ! -e "$APP_PATH/Frameworks/qemu-$unused-softmmu.framework" ]]
done

echo "$ARCHIVE_PATH"
