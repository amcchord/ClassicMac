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

[ -f "$GUEST_DIR/bin/GXMetal.bin" ] || die "GXMetal.bin was not produced"
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetal.pef"
for symbol in QARegisterEngine QARegisterDrawMethod RegistryEntrySearch; do
    strings "$GUEST_DIR/bin/GXMetal.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal PEF is missing required import $symbol"
done
file "$GUEST_DIR/bin/GXMetal.bin"
