#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CLASSICMAC_IOS_BUILD_DIR:-$ROOT_DIR/build/ipad}"
MARKETING_VERSION="${CLASSICMAC_MARKETING_VERSION:-1.0.0}"
BUILD_NUMBER="${CLASSICMAC_BUILD_NUMBER:-1}"
TEAM_ID="${APPLE_TEAM_ID:-${CLASSICMAC_TEAM_ID:-}}"
KEY_ID="${ASC_KEY_ID:-${APP_STORE_CONNECT_KEY_ID:-}}"
ISSUER_ID="${ASC_ISSUER_ID:-${APP_STORE_CONNECT_ISSUER_ID:-}}"
KEY_FILE="${ASC_KEY_FILE:-${APP_STORE_CONNECT_KEY_PATH:-}}"
ARCHIVE_PATH="${CLASSICMAC_ARCHIVE_PATH:-$BUILD_DIR/ClassicMac-iPad-$BUILD_NUMBER.xcarchive}"
EXPORT_DIR="${CLASSICMAC_EXPORT_DIR:-$BUILD_DIR/export-$BUILD_NUMBER}"
KEY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/classicmac-asc.XXXXXX")"
KEY_PATH="$KEY_DIR/AuthKey.p8"
EXPORT_OPTIONS_PLIST="$KEY_DIR/ExportOptions.plist"

: "${TEAM_ID:?Set APPLE_TEAM_ID to your Apple Developer Team ID.}"
: "${KEY_ID:?Set ASC_KEY_ID to your App Store Connect API Key ID.}"
: "${ISSUER_ID:?Set ASC_ISSUER_ID to your App Store Connect API Issuer ID.}"
: "${KEY_FILE:?Set ASC_KEY_FILE to the path of your AuthKey_*.p8 private key.}"

if [[ ! -f "$KEY_FILE" ]]; then
    echo "App Store Connect private key file not found: $KEY_FILE" >&2
    exit 1
fi

cleanup() {
    rm -f "$KEY_PATH" "$EXPORT_OPTIONS_PLIST"
    rmdir "$KEY_DIR" 2>/dev/null || true
}
trap cleanup EXIT
chmod 700 "$KEY_DIR"

cp "$KEY_FILE" "$KEY_PATH"
chmod 600 "$KEY_PATH"

cp "$ROOT_DIR/ios/ExportOptions.plist" "$EXPORT_OPTIONS_PLIST"
/usr/libexec/PlistBuddy -c "Set :teamID $TEAM_ID" "$EXPORT_OPTIONS_PLIST"

CLASSICMAC_SIGNING=automatic \
CLASSICMAC_TEAM_ID="$TEAM_ID" \
CLASSICMAC_MARKETING_VERSION="$MARKETING_VERSION" \
CLASSICMAC_BUILD_NUMBER="$BUILD_NUMBER" \
CLASSICMAC_ARCHIVE_PATH="$ARCHIVE_PATH" \
APP_STORE_CONNECT_KEY_ID="$KEY_ID" \
APP_STORE_CONNECT_ISSUER_ID="$ISSUER_ID" \
APP_STORE_CONNECT_KEY_PATH="$KEY_PATH" \
"$ROOT_DIR/scripts/build-ipad.sh"

if [[ -e "$EXPORT_DIR" ]]; then
    mv "$EXPORT_DIR" "$EXPORT_DIR.previous-$(date +%Y%m%d%H%M%S)"
fi
mkdir -p "$EXPORT_DIR"
# Xcode's IPA step starts a second rsync process by name. Keep that child on
# Apple's system rsync; a Homebrew rsync in PATH does not understand Xcode's
# extended-attribute flags and fails late with a misleading "Copy failed".
PATH=/usr/bin:/bin:/usr/sbin:/sbin xcodebuild -exportArchive \
    -archivePath "$ARCHIVE_PATH" \
    -exportPath "$EXPORT_DIR" \
    -exportOptionsPlist "$EXPORT_OPTIONS_PLIST" \
    -allowProvisioningUpdates \
    -authenticationKeyPath "$KEY_PATH" \
    -authenticationKeyID "$KEY_ID" \
    -authenticationKeyIssuerID "$ISSUER_ID"

IPA_PATH="$(find "$EXPORT_DIR" -maxdepth 1 -type f -name '*.ipa' -print -quit)"
if [[ -z "$IPA_PATH" ]]; then
    echo "Xcode did not produce an IPA in $EXPORT_DIR" >&2
    exit 1
fi

xcrun altool --validate-app \
    "$IPA_PATH" \
    --apiKey "$KEY_ID" \
    --apiIssuer "$ISSUER_ID" \
    --p8-file-path "$KEY_PATH"

xcrun altool --upload-package \
    "$IPA_PATH" \
    --apiKey "$KEY_ID" \
    --apiIssuer "$ISSUER_ID" \
    --p8-file-path "$KEY_PATH" \
    --show-progress \
    --wait

echo "Uploaded ClassicMac $MARKETING_VERSION ($BUILD_NUMBER) to App Store Connect."
