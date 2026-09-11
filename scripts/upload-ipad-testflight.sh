#!/bin/bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CLASSICMAC_IOS_BUILD_DIR:-$ROOT_DIR/build/ipad}"
MARKETING_VERSION="${CLASSICMAC_MARKETING_VERSION:-1.0.0}"
BUILD_NUMBER="${CLASSICMAC_BUILD_NUMBER:-1}"
TEAM_ID="${CLASSICMAC_TEAM_ID:-7PTN7E8EDS}"
ARCHIVE_PATH="${CLASSICMAC_ARCHIVE_PATH:-$BUILD_DIR/ClassicMac-iPad-$BUILD_NUMBER.xcarchive}"
EXPORT_DIR="${CLASSICMAC_EXPORT_DIR:-$BUILD_DIR/export-$BUILD_NUMBER}"
KEY_DIR="$(mktemp -d "${TMPDIR:-/tmp}/classicmac-asc.XXXXXX")"
KEY_PATH="$KEY_DIR/AuthKey.p8"
CREDENTIALS_PATH="$KEY_DIR/credentials.json"

cleanup() {
    rm -f "$KEY_PATH" "$CREDENTIALS_PATH"
    rmdir "$KEY_DIR" 2>/dev/null || true
}
trap cleanup EXIT
chmod 700 "$KEY_DIR"

curl --fail --silent --show-error \
    --request POST \
    --header 'Content-Type: application/json' \
    --data '{"service":"app-store-connect","project":"ClassicMac-iPad"}' \
    http://127.0.0.1:8472/api/keys/provision \
    --output "$CREDENTIALS_PATH"
chmod 600 "$CREDENTIALS_PATH"

KEY_ID="$(jq -er '.entry.secrets.APP_STORE_CONNECT_KEY_ID' "$CREDENTIALS_PATH")"
ISSUER_ID="$(jq -er '.entry.secrets.APP_STORE_CONNECT_ISSUER_ID' "$CREDENTIALS_PATH")"
jq -er '.entry.secrets.APP_STORE_CONNECT_PRIVATE_KEY' "$CREDENTIALS_PATH" >"$KEY_PATH"
chmod 600 "$KEY_PATH"

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
    -exportOptionsPlist "$ROOT_DIR/ios/ExportOptions.plist" \
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
