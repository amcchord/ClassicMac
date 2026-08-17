#!/usr/bin/env bash
#
# notarize.sh - Submit dist/ClassicMac.app to Apple's notary service, staple
# the resulting ticket to the bundle, and produce dist/ClassicMac.zip ready
# for distribution.
#
# Prerequisites:
#   - dist/ClassicMac.app built and signed with a Developer ID certificate
#     (scripts/bundle-qemu.sh does this when the certificate is present).
#   - App Store Connect API credentials in NOTARY_KEY, NOTARY_KEY_ID, and
#     NOTARY_ISSUER_ID (omit the issuer for an Individual API Key), or notary
#     credentials stored in the keychain:
#       xcrun notarytool store-credentials classicmac-notary \
#         --apple-id <apple-id> --team-id <team-id> --password <app-password>
#
# Idempotent: safe to re-run; the zip is rebuilt and the app re-stapled.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT_DIR/dist/ClassicMac.app"
ZIP="$ROOT_DIR/dist/ClassicMac.zip"
PROFILE="${NOTARY_PROFILE:-classicmac-notary}"

log() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ -d "$APP" ] || die "dist/ClassicMac.app not found. Run scripts/bundle-qemu.sh first."

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

# Refuse to notarize an ad-hoc signed bundle; the notary service rejects it.
AUTHORITY="$(codesign -dvv "$APP" 2>&1 | grep '^Authority=' | head -1 || true)"
case "$AUTHORITY" in
  *"Developer ID Application"*) ;;
  *) die "App is not signed with a Developer ID certificate (found: ${AUTHORITY:-no authority}). Re-run scripts/bundle-qemu.sh with the certificate in the keychain." ;;
esac

# ---------------------------------------------------------------------------
# 1. Zip and submit
# ---------------------------------------------------------------------------
log "Zipping app for submission"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

log "Submitting to Apple notary service (this can take a few minutes)"
SUBMIT_OUTPUT="$(xcrun notarytool submit "$ZIP" "${NOTARY_AUTH[@]}" --wait 2>&1 | tee /dev/stderr)"

SUBMISSION_ID="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  id:/{print $2; exit}')"
STATUS="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^  status:/{print $2}' | tail -1)"

if [ "$STATUS" != "Accepted" ]; then
  log "Notarization failed (status: ${STATUS:-unknown}). Fetching log:"
  if [ -n "$SUBMISSION_ID" ]; then
    xcrun notarytool log "$SUBMISSION_ID" "${NOTARY_AUTH[@]}" || true
  fi
  die "Notarization was not accepted."
fi

# ---------------------------------------------------------------------------
# 2. Staple the ticket and rebuild the distributable zip
# ---------------------------------------------------------------------------
log "Stapling notarization ticket to the app"
xcrun stapler staple "$APP"

log "Rebuilding distribution zip with the stapled ticket"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

# ---------------------------------------------------------------------------
# 3. Verify Gatekeeper acceptance
# ---------------------------------------------------------------------------
log "Verifying with Gatekeeper"
spctl --assess --type execute --verbose=2 "$APP" || die "Gatekeeper rejected the app."
xcrun stapler validate "$APP" || die "Stapled ticket failed validation."

log "Done. Distributable: $ZIP"
