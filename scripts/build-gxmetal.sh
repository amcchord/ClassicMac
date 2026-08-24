#!/usr/bin/env bash
# Build the PowerPC CFM GXMetal QuickDraw 3D RAVE engine.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN="$ROOT_DIR/vendor/Retro68-build/toolchain"
GUEST_DIR="$ROOT_DIR/gxmetal/guest"
ICON_MASTER="$GUEST_DIR/art/GXMetalIcon-master.gif"
ICON_MASTER_SHA256="e0cca2302487408cb78d5963354ecf14f1f7ccd903250be33d15a85535b17bf3"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

for tool in m68k-apple-macos-gcc powerpc-apple-macos-gcc MakePEF Rez; do
    [ -x "$TOOLCHAIN/bin/$tool" ] || \
        die "$tool is missing; run scripts/build-ppcvid-ndrv.sh first"
done
[ -f "$TOOLCHAIN/powerpc-apple-macos/include/RAVESystem.h" ] || \
    die "RAVE Universal Interfaces are missing; run scripts/build-ppcvid-ndrv.sh first"
[ -f "$TOOLCHAIN/powerpc-apple-macos/lib/libQuickDraw3DRAVELib.a" ] || \
    die "QuickDraw 3D RAVE import library is missing"
[ -f "$ICON_MASTER" ] || die "the canonical GXMetal icon master is missing"
ACTUAL_ICON_SHA256="$(shasum -a 256 "$ICON_MASTER" | awk '{print $1}')"
[ "$ACTUAL_ICON_SHA256" = "$ICON_MASTER_SHA256" ] || \
    die "the canonical GXMetal icon no longer matches the supplied artwork"

export PATH="$TOOLCHAIN/bin:$PATH"

make -C "$GUEST_DIR" clean
make -C "$GUEST_DIR" \
    RINCLUDES="$TOOLCHAIN/universal/RIncludes"

for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin; do
    [ -f "$GUEST_DIR/bin/$artifact" ] || die "$artifact was not produced"
done
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetal.bin" --type shlb --creator tnsl \
    --require-flags 0x0400 --forbid-flags 0x0180
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalInput.bin" --type shlb --creator insp \
    --require-flags 0x0400 --forbid-flags 0x0180
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalStartup.bin" --type INIT --creator GXMT \
    --require-flags 0x0400 --forbid-flags 0x0180
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalInstaller.bin" --type APPL --creator GXMT \
    --require-flags 0x2000 --forbid-flags 0x0100
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalTest.bin" --type APPL --creator GXMT \
    --require-flags 0x2000 --forbid-flags 0x0100
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetal.pef"
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetalInput.pef"
DeRez -only tnsl "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'tnsl' (0)" >/dev/null || \
    die "GXMetal resource fork is missing the RAVE tnsl discovery marker"
DeRez -only ftag "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'ftag' (0)" >/dev/null || \
    die "GXMetal resource fork is missing the CFM fragment tag"
DeRez -only vers "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'vers' (1)" >/dev/null || \
    die "GXMetal resource fork is missing its fragment version"
DeRez -only INIT "$GUEST_DIR/bin/GXMetal Startup" | \
    grep -F "data 'INIT' (128" >/dev/null || \
    die "GXMetal Startup is missing its executable INIT resource"
DeRez -only ICN# "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'ICN#' (-16455" >/dev/null || \
    die "GXMetal resource fork is missing its custom Finder icon"
DeRez -only ICN# "$GUEST_DIR/bin/GXMetal Input" | \
    grep -F "data 'ICN#' (-16455" >/dev/null || \
    die "GXMetal Input is missing its custom Finder icon"
DeRez -only ICN# "$GUEST_DIR/bin/GXMetal Startup" | \
    grep -F "data 'ICN#' (-16455" >/dev/null || \
    die "GXMetal Startup is missing its custom Finder icon"

# All five visible GXMetal components must carry the same icon pixels. Resource
# IDs legitimately differ between shared libraries/extensions and applications,
# so compare only the hex payloads emitted by DeRez.
ICON_CHECK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/gxmetal-icons.XXXXXX")"
trap 'rm -rf "$ICON_CHECK_DIR"' EXIT
ICON_ARTIFACTS=(
    "$GUEST_DIR/bin/GXMetal"
    "$GUEST_DIR/bin/GXMetal Input"
    "$GUEST_DIR/bin/GXMetal Startup"
    "$GUEST_DIR/bin/GXMetalInstaller"
    "$GUEST_DIR/bin/GXMetalTest"
)
for resource_type in icl8 icl4 ICN# ics8 ics4 ics# ICON; do
    reference_payload="$ICON_CHECK_DIR/$resource_type.reference"
    DeRez -only "$resource_type" "${ICON_ARTIFACTS[0]}" | \
        sed -n '/\$"/p' > "$reference_payload"
    [ -s "$reference_payload" ] || \
        die "GXMetal is missing its $resource_type icon member"
    for artifact in "${ICON_ARTIFACTS[@]:1}"; do
        artifact_payload="$ICON_CHECK_DIR/$resource_type.$(basename "$artifact")"
        DeRez -only "$resource_type" "$artifact" | \
            sed -n '/\$"/p' > "$artifact_payload"
        cmp -s "$reference_payload" "$artifact_payload" || \
            die "$(basename "$artifact") does not share GXMetal's $resource_type icon"
    done
done
rm -rf "$ICON_CHECK_DIR"
trap - EXIT

for symbol in QARegisterEngine QARegisterDrawMethod RegistryEntrySearch \
    GXMetalGetInputButtonState GXMetalGetInputState GXMetalGetInputEvents \
    GXMetalSetRelativeInputMode; do
    strings "$GUEST_DIR/bin/GXMetal.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal PEF is missing required import $symbol"
done
for symbol in ISpDriver_CheckConfiguration ISpDriver_FindAndLoadDevices \
    ISpDriver_DisposeDevices ISpDriver_Tickle ISpDevice_New \
    ISpDevice_Dispose ISpElement_New ISpElement_Dispose \
    ISpElement_PushSimpleData GXMetalGetInputButtonState \
    GXMetalGetInputState GXMetalGetInputEvents \
    GXMetalSetRelativeInputMode; do
    strings "$GUEST_DIR/bin/GXMetalInput.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal Input PEF is missing required symbol $symbol"
done
for symbol in QAInit QAExit QADeviceGetFirstEngine QADeviceGetNextEngine \
    QADrawContextNew QATextureNew QATextureDetach QABitmapNew \
    QABitmapDetach QABitmapDelete Microseconds; do
    strings "$GUEST_DIR/bin/GXMetalTest.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal Test PEF is missing required import $symbol"
done
for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin; do
    file "$GUEST_DIR/bin/$artifact"
    strings "$GUEST_DIR/bin/$artifact" | grep -F "2.0.6" >/dev/null || \
        die "$artifact does not report GXMetal version 2.0.6"
done
