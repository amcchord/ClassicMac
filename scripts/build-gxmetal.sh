#!/usr/bin/env bash
# Build the PowerPC CFM GXMetal QuickDraw 3D RAVE engine.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN="$ROOT_DIR/vendor/Retro68-build/toolchain"
GUEST_DIR="$ROOT_DIR/gxmetal/guest"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

for tool in powerpc-apple-macos-gcc MakePEF Rez; do
    [ -x "$TOOLCHAIN/bin/$tool" ] || \
        die "$tool is missing; run scripts/build-ppcvid-ndrv.sh first"
done
[ -f "$TOOLCHAIN/powerpc-apple-macos/include/RAVESystem.h" ] || \
    die "RAVE Universal Interfaces are missing; run scripts/build-ppcvid-ndrv.sh first"
[ -f "$TOOLCHAIN/powerpc-apple-macos/lib/libQuickDraw3DRAVELib.a" ] || \
    die "QuickDraw 3D RAVE import library is missing"

export PATH="$TOOLCHAIN/bin:$PATH"

make -C "$GUEST_DIR" clean
make -C "$GUEST_DIR" \
    RINCLUDES="$TOOLCHAIN/universal/RIncludes"

for artifact in GXMetal.bin GXMetalInstaller.bin GXMetalTest.bin; do
    [ -f "$GUEST_DIR/bin/$artifact" ] || die "$artifact was not produced"
done
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetal.bin" --type shlb --creator tnsl
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalInstaller.bin" --type APPL --creator GXMT
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalTest.bin" --type APPL --creator GXMT
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetal.pef"
DeRez -only tnsl "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'tnsl' (0)" >/dev/null || \
    die "GXMetal resource fork is missing the RAVE tnsl discovery marker"
DeRez -only ftag "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'ftag' (0)" >/dev/null || \
    die "GXMetal resource fork is missing the CFM fragment tag"
DeRez -only vers "$GUEST_DIR/bin/GXMetal" | \
    grep -F "data 'vers' (1)" >/dev/null || \
    die "GXMetal resource fork is missing its fragment version"
for symbol in QARegisterEngine QARegisterDrawMethod RegistryEntrySearch; do
    strings "$GUEST_DIR/bin/GXMetal.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal PEF is missing required import $symbol"
done
for symbol in QAInit QAExit QADeviceGetFirstEngine QADeviceGetNextEngine \
    QADrawContextNew QATextureNew QATextureDetach QABitmapNew \
    QABitmapDetach QABitmapDelete Microseconds; do
    strings "$GUEST_DIR/bin/GXMetalTest.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal Test PEF is missing required import $symbol"
done
for artifact in GXMetal.bin GXMetalInstaller.bin GXMetalTest.bin; do
    file "$GUEST_DIR/bin/$artifact"
done
