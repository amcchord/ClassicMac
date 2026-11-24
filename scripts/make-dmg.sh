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
#
# Environment overrides (defaults unchanged): DIST_DIR (default dist/, holding
# ClassicMac.app), DIST_NAME (default ClassicMac: DIST_DIR/DIST_NAME.dmg, and
# the notarized zip's name) and DMG_VOLUME_NAME (default ClassicMac).

set -euo pipefail
export COPYFILE_DISABLE=1

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
DIST_NAME="${DIST_NAME:-ClassicMac}"
export DIST_DIR DIST_NAME
APP="$DIST_DIR/ClassicMac.app"
DMG="$DIST_DIR/$DIST_NAME.dmg"
STAGING=""
VOLNAME="${DMG_VOLUME_NAME:-ClassicMac}"
PROFILE="${NOTARY_PROFILE:-classicmac-notary}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

cleanup_dmg() {
  local status=$?
  trap - EXIT HUP INT TERM
  set +e
  if [ -n "$STAGING" ] && [ -d "$STAGING" ]; then
    rm -rf -- "$STAGING"
  fi
  exit "$status"
}

trap cleanup_dmg EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

[ -d "$APP" ] || die "$APP not found. Run scripts/bundle-qemu.sh first."

SIGN_IDENTITY="${SIGN_IDENTITY:-}"
if [ -z "$SIGN_IDENTITY" ]; then
  SIGN_IDENTITY="$(security find-identity -v -p codesigning | awk -F'"' '/Developer ID Application/{print $2; exit}')"
fi
[ -n "$SIGN_IDENTITY" ] || die "No Developer ID Application certificate found in the keychain."

# Notary credentials: use an explicitly supplied API key first, then the
# keychain profile, then Apple ID credentials. This keeps automation from
# depending on an unlocked login keychain.
if [ -n "${NOTARY_KEY:-}" ] && [ -n "${NOTARY_KEY_ID:-}" ]; then
  NOTARY_AUTH=(--key "$NOTARY_KEY" --key-id "$NOTARY_KEY_ID")
  if [ -n "${NOTARY_ISSUER_ID:-}" ]; then
    NOTARY_AUTH+=(--issuer "$NOTARY_ISSUER_ID")
  fi
  xcrun notarytool history "${NOTARY_AUTH[@]}" >/dev/null 2>&1 || \
    die "The supplied NOTARY_KEY credentials were rejected by Apple."
else
  NOTARY_AUTH=(--keychain-profile "$PROFILE")
  if ! xcrun notarytool history "${NOTARY_AUTH[@]}" >/dev/null 2>&1; then
    if [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_TEAM_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then
      log "Keychain profile '$PROFILE' unavailable; using NOTARY_* Apple ID credentials"
      NOTARY_AUTH=(--apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD")
    else
      die "No usable notary credentials. Set NOTARY_KEY and NOTARY_KEY_ID (plus NOTARY_ISSUER_ID for a Team API Key), unlock or recreate keychain profile '$PROFILE', or set NOTARY_APPLE_ID, NOTARY_TEAM_ID and NOTARY_PASSWORD."
    fi
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
STAGING="$(mktemp -d "$DIST_DIR/.dmg-staging.XXXXXX")"
ditto --norsrc "$APP" "$STAGING/ClassicMac.app"
find "$STAGING" -name '._*' -delete
ln -s /Applications "$STAGING/Applications"

log "Creating $DMG"
rm -f "$DMG"
hdiutil create -volname "$VOLNAME" -srcfolder "$STAGING" -ov -format UDZO "$DMG"
rm -rf "$STAGING"
STAGING=""

# ---------------------------------------------------------------------------
# 3. Sign, notarize, and staple the DMG
# ---------------------------------------------------------------------------
log "Signing DMG"
codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"

log "Submitting DMG to Apple notary service (this can take a few minutes)"
SUBMIT_STATUS=0
SUBMIT_OUTPUT="$(xcrun notarytool submit "$DMG" "${NOTARY_AUTH[@]}" --wait 2>&1 | tee /dev/stderr)" || SUBMIT_STATUS=$?

SUBMISSION_ID="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  id:/{print $2; exit}')"
STATUS="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  status:/{print $2}' | tail -1)"

if [ "$STATUS" != "Accepted" ] || [ "$SUBMIT_STATUS" -ne 0 ]; then
  log "Notarization failed (status: ${STATUS:-unknown}, exit: $SUBMIT_STATUS). Fetching log:"
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
