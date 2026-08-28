#!/usr/bin/env bash
# Build the PowerPC CFM GXMetal QuickDraw 3D RAVE engine.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLCHAIN="$ROOT_DIR/vendor/Retro68-build/toolchain"
GUEST_DIR="$ROOT_DIR/gxmetal/guest"
ICON_MASTER="$GUEST_DIR/art/GXMetalIcon-master.png"
ICON_MASTER_SHA256="cb47f919bdcc6b93ce7fa0114e82601db816f1c81e63ef18125f315b560273db"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

for tool in m68k-apple-macos-gcc powerpc-apple-macos-gcc \
            powerpc-apple-macos-nm MakePEF Rez; do
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
                GXMetalInstaller.bin GXMetalTest.bin GXMetalAGLProbe.bin \
                GXMetalEngineSelectionProbe.bin; do
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
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalAGLProbe.bin" --type APPL --creator GXMA \
    --require-flags 0x2000 --forbid-flags 0x0100
python3 "$ROOT_DIR/gxmetal/tools/verify_macbinary.py" \
    "$GUEST_DIR/bin/GXMetalEngineSelectionProbe.bin" \
    --type APPL --creator GXMS \
    --require-flags 0x2000 --forbid-flags 0x0100
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetal.pef"
python3 "$ROOT_DIR/gxmetal/tools/pef_set_init.py" \
    --verify "$GUEST_DIR/bin/GXMetalInput.pef"

# Unreal Tournament v348 exposed a classic CFM layout cliff: a valid driver
# whose final QARegisterEngine import stub started at 0x148c4 stopped engine
# discovery after the three identity Gestalts, while the otherwise identical
# compact build at 0x14804 completed accelerated startup. Keep the last proven
# passing layout as a build gate until a later address is explicitly qualified
# in the Mac OS 9 VM; this turns future driver growth into a clear build error
# instead of another game-only regression.
GXMETAL_NM="$TOOLCHAIN/bin/powerpc-apple-macos-nm"
GXMETAL_UT_LAYOUT_MAX_HEX="00014804"
GXMETAL_QA_REGISTER_HEX="$($GXMETAL_NM -n \
    "$GUEST_DIR/bin/GXMetal.xcoff" | \
    awk '$2 == "T" && $3 == ".QARegisterEngine" { print $1; exit }')"
[ -n "$GXMETAL_QA_REGISTER_HEX" ] || \
    die "GXMetal XCOFF is missing the QARegisterEngine import stub"
if (( 16#$GXMETAL_QA_REGISTER_HEX > 16#$GXMETAL_UT_LAYOUT_MAX_HEX )); then
    die "GXMetal CFM layout regressed: QARegisterEngine is at 0x$GXMETAL_QA_REGISTER_HEX (last UT-qualified maximum 0x$GXMETAL_UT_LAYOUT_MAX_HEX)"
fi
printf 'GXMetal XCOFF: UT-qualified CFM layout, QARegisterEngine at 0x%s\n' \
    "$GXMETAL_QA_REGISTER_HEX"
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

# All seven visible GXMetal components must carry the same icon pixels. Resource
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
    "$GUEST_DIR/bin/GXMetal AGL Probe"
    "$GUEST_DIR/bin/GXMetal RAVE Selection"
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
    GetCurrentProcess GetProcessInformation NewPtrSysClear \
    GXMetalGetInputButtonState GXMetalGetInputState \
    GXMetalGetInputEvents \
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
strings "$GUEST_DIR/bin/GXMetalTest.pef" | \
    grep -F "ATI-private-update" >/dev/null ||
    die "GXMetal Test PEF is missing ATI private texture-update coverage"
strings "$GUEST_DIR/bin/GXMetalTest.pef" | \
    grep -F "ATI-private-readback" >/dev/null ||
    die "GXMetal Test PEF is missing ATI private readback coverage"
for symbol in OpenGLLibrary aglChoosePixelFormat aglCreateContext \
    aglSetDrawable aglSetCurrentContext glGetString glBegin glReadPixels \
    glGenTextures glTexImage2D glBlendFunc glDepthFunc glDeleteTextures \
    aglDestroyContext; do
    strings "$GUEST_DIR/bin/GXMetalAGLProbe.pef" | grep -F "$symbol" >/dev/null ||
        die "GXMetal AGL Probe PEF is missing required dynamic symbol $symbol"
done
for symbol in QAInit QAExit QADeviceGetFirstEngine QADeviceGetNextEngine \
    QAEngineGestalt Q3Initialize Q3Exit Q3Renderer_NewFromType \
    Q3InteractiveRenderer_GetPreferences \
    Q3InteractiveRenderer_SetPreferences \
    Q3InteractiveRenderer_CountRAVEDrawContexts \
    Q3InteractiveRenderer_GetRAVEDrawContexts Q3View_StartRendering; do
    strings "$GUEST_DIR/bin/GXMetalEngineSelectionProbe.pef" | \
        grep -F "$symbol" >/dev/null || \
        die "GXMetal RAVE Selection PEF is missing required import $symbol"
done
for artifact in GXMetal.bin GXMetalInput.bin GXMetalStartup.bin \
                GXMetalInstaller.bin GXMetalTest.bin GXMetalAGLProbe.bin \
                GXMetalEngineSelectionProbe.bin; do
    file "$GUEST_DIR/bin/$artifact"
    strings "$GUEST_DIR/bin/$artifact" | grep -F "2.2.4" >/dev/null || \
        die "$artifact does not report GXMetal version 2.2.4"
done
