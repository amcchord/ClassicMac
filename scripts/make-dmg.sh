#!/usr/bin/env bash
#
# make-dmg.sh - Package the signed dist/ClassicMac.app into a distributable
# disk image with a drag-to-Applications layout, then sign, notarize, and
# staple the DMG. The result is dist/ClassicMac.dmg, ready to share.
#
# The app inside must already be Developer ID signed (scripts/bundle-qemu.sh);
# if it has not been notarized/stapled yet this script runs scripts/notarize.sh
# first so recipients get a clean Gatekeeper experience even offline.
#
# Typical release flow:
#   scripts/bundle-qemu.sh
#   scripts/make-dmg.sh
#
# Idempotent: the DMG is rebuilt from scratch on every run.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT_DIR/dist/ClassicMac.app"
DMG="$ROOT_DIR/dist/ClassicMac.dmg"
STAGING="$ROOT_DIR/dist/dmg-staging"
VOLNAME="ClassicMac"
PROFILE="${NOTARY_PROFILE:-classicmac-notary}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ -d "$APP" ] || die "dist/ClassicMac.app not found. Run scripts/bundle-qemu.sh first."

SIGN_IDENTITY="${SIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
  SIGN_IDENTITY="$(security find-identity -v -p codesigning | awk -F'"' '/Developer ID Application/{print $2; exit}')"
fi
[ -n "$SIGN_IDENTITY" ] || die "No Developer ID Application certificate found in the keychain."

# Notary credentials: prefer the keychain profile, but fall back to explicit
# NOTARY_APPLE_ID / NOTARY_TEAM_ID / NOTARY_PASSWORD env vars when the
# keychain is unavailable (e.g. locked in a headless session).
NOTARY_AUTH=(--keychain-profile "$PROFILE")
if ! xcrun notarytool history "${NOTARY_AUTH[@]}" >/dev/null 2>&1; then
  if [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_TEAM_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then
    log "Keychain profile '$PROFILE' unavailable; using NOTARY_* env credentials"
    NOTARY_AUTH=(--apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD")
  else
    die "Notary keychain profile '$PROFILE' is unavailable (keychain locked or profile missing). Unlock the login keychain or re-run 'xcrun notarytool store-credentials $PROFILE', or set NOTARY_APPLE_ID, NOTARY_TEAM_ID and NOTARY_PASSWORD."
  fi
fi

# ---------------------------------------------------------------------------
# 1. Make sure the app itself is notarized and stapled
# ---------------------------------------------------------------------------
if xcrun stapler validate "$APP" >/dev/null 2>&1; then
  log "App already has a stapled notarization ticket"
else
  log "App not stapled yet; running scripts/notarize.sh"
  bash "$ROOT_DIR/scripts/notarize.sh"
fi

# ---------------------------------------------------------------------------
# 2. Build the DMG (app + Applications symlink)
# ---------------------------------------------------------------------------
log "Assembling DMG staging folder"
rm -rf "$STAGING"
mkdir -p "$STAGING"
cp -R "$APP" "$STAGING/ClassicMac.app"
ln -s /Applications "$STAGING/Applications"

log "Creating $DMG"
rm -f "$DMG"
hdiutil create -volname "$VOLNAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG"
rm -rf "$STAGING"

# ---------------------------------------------------------------------------
# 3. Sign, notarize, and staple the DMG
# ---------------------------------------------------------------------------
log "Signing DMG"
codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"

log "Submitting DMG to Apple notary service (this can take a few minutes)"
SUBMIT_OUTPUT="$(xcrun notarytool submit "$DMG" "${NOTARY_AUTH[@]}" --wait 2>&1 | tee /dev/stderr)"

SUBMISSION_ID="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  id:/{print $2; exit}')"
STATUS="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  status:/{print $2}' | tail -1)"

if [ "$STATUS" != "Accepted" ]; then
  log "Notarization failed (status: ${STATUS:-unknown}). Fetching log:"
  if [ -n "$SUBMISSION_ID" ]; then
    xcrun notarytool log "$SUBMISSION_ID" "${NOTARY_AUTH[@]}" || true
  fi
  die "DMG notarization was not accepted."
fi

log "Stapling notarization ticket to the DMG"
xcrun stapler staple "$DMG"

# ---------------------------------------------------------------------------
# 4. Verify
# ---------------------------------------------------------------------------
log "Verifying DMG"
spctl --assess --type open --context context:primary-signature --verbose=2 "$DMG" || die "Gatekeeper rejected the DMG."
xcrun stapler validate "$DMG" || die "Stapled ticket failed validation."

log "Done. Distributable disk image: $DMG"
