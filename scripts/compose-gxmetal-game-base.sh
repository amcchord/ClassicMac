#!/bin/sh
# Compose one already-installed classic Mac game into an immutable sweep base.
#
# The donor and base images are mounted read-only/read-write respectively only
# after the output has been created as an APFS clone (or a full copy fallback).
# Resource forks, Finder metadata, and extended attributes are preserved by
# ditto.  The input images are never modified.

set -eu

usage() {
  echo "usage: $0 BASE.img DONOR.img SOURCE_PATH OUTPUT.img [DEST_PATH]" >&2
  exit 2
}

die() {
  echo "compose-gxmetal-game-base: $*" >&2
  exit 1
}

absolute_existing_file() {
  candidate=$1
  [ -f "$candidate" ] || die "image does not exist: $candidate"
  candidate_dir=$(cd "$(dirname "$candidate")" && pwd -P)
  printf '%s/%s\n' "$candidate_dir" "$(basename "$candidate")"
}

absolute_new_file() {
  candidate=$1
  candidate_dir=$(cd "$(dirname "$candidate")" && pwd -P)
  printf '%s/%s\n' "$candidate_dir" "$(basename "$candidate")"
}

validate_relative_path() {
  candidate=$1
  case "$candidate" in
    ''|/*|.|..|*//*|*/../*|../*|*/..|*/./*|./*)
      die "path must be a nonempty normalized relative path: $candidate"
      ;;
  esac
}

[ "$#" -eq 4 ] || [ "$#" -eq 5 ] || usage

BASE_IMAGE=$(absolute_existing_file "$1")
DONOR_IMAGE=$(absolute_existing_file "$2")
SOURCE_PATH=$3
OUTPUT_IMAGE=$(absolute_new_file "$4")
DEST_PATH=${5:-$SOURCE_PATH}

validate_relative_path "$SOURCE_PATH"
validate_relative_path "$DEST_PATH"
[ "$BASE_IMAGE" != "$DONOR_IMAGE" ] || die "base and donor must differ"
[ ! -w "$BASE_IMAGE" ] || \
  die "base must be host-read-only before composition: $BASE_IMAGE"
[ ! -w "$DONOR_IMAGE" ] || \
  die "donor must be host-read-only before composition: $DONOR_IMAGE"
[ ! -e "$OUTPUT_IMAGE" ] || die "refusing to overwrite output: $OUTPUT_IMAGE"
[ ! -e "$OUTPUT_IMAGE.composition.txt" ] || \
  die "refusing to overwrite sidecar: $OUTPUT_IMAGE.composition.txt"

BASE_SHA256=$(shasum -a 256 "$BASE_IMAGE" | awk '{print $1}')
DONOR_SHA256=$(shasum -a 256 "$DONOR_IMAGE" | awk '{print $1}')

if cp -c "$BASE_IMAGE" "$OUTPUT_IMAGE" 2>/dev/null; then
  CLONE_METHOD=clonefile
else
  cp -p "$BASE_IMAGE" "$OUTPUT_IMAGE"
  CLONE_METHOD=full-copy
fi
chmod u+w "$OUTPUT_IMAGE"

DONOR_MOUNT=$(mktemp -d /tmp/gxmetal-donor.XXXXXX)
OUTPUT_MOUNT=$(mktemp -d /tmp/gxmetal-output.XXXXXX)
DONOR_ATTACHED=0
OUTPUT_ATTACHED=0
SUCCESS=0

cleanup() {
  if [ "$OUTPUT_ATTACHED" -eq 1 ]; then
    hdiutil detach "$OUTPUT_MOUNT" >/dev/null 2>&1 || true
  fi
  if [ "$DONOR_ATTACHED" -eq 1 ]; then
    hdiutil detach "$DONOR_MOUNT" >/dev/null 2>&1 || true
  fi
  rmdir "$OUTPUT_MOUNT" "$DONOR_MOUNT" 2>/dev/null || true
  if [ "$SUCCESS" -ne 1 ]; then
    rm -f "$OUTPUT_IMAGE" "$OUTPUT_IMAGE.composition.txt"
  fi
}
trap cleanup EXIT HUP INT TERM

hdiutil attach -readonly -nobrowse -noverify \
  -mountpoint "$DONOR_MOUNT" "$DONOR_IMAGE" >/dev/null
DONOR_ATTACHED=1
hdiutil attach -nobrowse -noverify \
  -mountpoint "$OUTPUT_MOUNT" "$OUTPUT_IMAGE" >/dev/null
OUTPUT_ATTACHED=1

SOURCE_ITEM=$DONOR_MOUNT/$SOURCE_PATH
DEST_ITEM=$OUTPUT_MOUNT/$DEST_PATH
[ -e "$SOURCE_ITEM" ] || die "donor item is absent: $SOURCE_PATH"
[ ! -e "$DEST_ITEM" ] || die "destination already exists: $DEST_PATH"
mkdir -p "$(dirname "$DEST_ITEM")"
ditto "$SOURCE_ITEM" "$DEST_ITEM"
[ -e "$DEST_ITEM" ] || die "copy did not create destination: $DEST_PATH"

hdiutil detach "$OUTPUT_MOUNT" >/dev/null
OUTPUT_ATTACHED=0
hdiutil detach "$DONOR_MOUNT" >/dev/null
DONOR_ATTACHED=0
rmdir "$OUTPUT_MOUNT" "$DONOR_MOUNT"

BASE_AFTER_SHA256=$(shasum -a 256 "$BASE_IMAGE" | awk '{print $1}')
DONOR_AFTER_SHA256=$(shasum -a 256 "$DONOR_IMAGE" | awk '{print $1}')
[ "$BASE_AFTER_SHA256" = "$BASE_SHA256" ] || \
  die "base image changed during composition: $BASE_IMAGE"
[ "$DONOR_AFTER_SHA256" = "$DONOR_SHA256" ] || \
  die "donor image changed during read-only composition: $DONOR_IMAGE"
OUTPUT_SHA256=$(shasum -a 256 "$OUTPUT_IMAGE" | awk '{print $1}')
chmod a-w "$OUTPUT_IMAGE"
{
  printf 'schema=1\n'
  printf 'clone_method=%s\n' "$CLONE_METHOD"
  printf 'base_image=%s\n' "$BASE_IMAGE"
  printf 'base_sha256=%s\n' "$BASE_SHA256"
  printf 'donor_image=%s\n' "$DONOR_IMAGE"
  printf 'donor_sha256=%s\n' "$DONOR_SHA256"
  printf 'source_path=%s\n' "$SOURCE_PATH"
  printf 'destination_path=%s\n' "$DEST_PATH"
  printf 'output_image=%s\n' "$OUTPUT_IMAGE"
  printf 'output_sha256=%s\n' "$OUTPUT_SHA256"
} > "$OUTPUT_IMAGE.composition.txt"

SUCCESS=1
printf 'Composed %s -> %s\n' "$SOURCE_PATH" "$DEST_PATH"
printf 'Base SHA-256:   %s\n' "$BASE_SHA256"
printf 'Donor SHA-256:  %s\n' "$DONOR_SHA256"
printf 'Output SHA-256: %s\n' "$OUTPUT_SHA256"
printf 'Clone method:   %s\n' "$CLONE_METHOD"
printf 'Manifest:       %s\n' "$OUTPUT_IMAGE.composition.txt"
