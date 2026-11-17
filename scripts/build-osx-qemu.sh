#!/usr/bin/env bash
# Build the PowerPC Mac OS X engine separately from ClassicMac's QEMU 11.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$root/vendor/qemu-osx"
build_dir="$source_dir/build"
revision=98f88efe8f07067667b8d9b1a5ce9551d34036c1
repository=https://github.com/qemu/qemu.git
patch="$root/osx/retroboot-qemu10.patch"

if [ ! -e "$source_dir" ]; then
    mkdir -p "$source_dir"
    git -C "$source_dir" init -q
fi
if [ -d "$source_dir/.git" ]; then
    if ! git -C "$source_dir" cat-file -e "$revision^{commit}" 2>/dev/null; then
        git -C "$source_dir" fetch --depth 1 "$repository" "$revision"
    fi
    if ! git -C "$source_dir" rev-parse --verify HEAD >/dev/null 2>&1; then
        git -C "$source_dir" checkout --detach "$revision"
    fi
    [ "$(git -C "$source_dir" rev-parse HEAD)" = "$revision" ]
    if [ ! -e "$source_dir/hw/display/ati_gl.c" ]; then
        git -C "$source_dir" apply --check "$patch"
        git -C "$source_dir" apply "$patch"
    fi
else
    # Release source archives contain the already patched tree.
    [ "$(cat "$source_dir/CLASSICMAC-SOURCE-REVISION")" = "$revision" ]
fi

host="$(uname -s)"
case "$host" in
    Darwin)
        export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"
        crypto=--disable-gcrypt
        audio=coreaudio
        cocoa=--enable-cocoa
        jobs="$(sysctl -n hw.ncpu)"
        suffix=
        ;;
    Linux)
        crypto=--enable-gcrypt
        audio=pa
        cocoa=--disable-cocoa
        jobs=4
        suffix=
        ;;
    MINGW*|MSYS*)
        crypto=--enable-gcrypt
        audio=dsound
        cocoa=--disable-cocoa
        jobs=3
        suffix=.exe
        cp "$root/osx/ati-gl-windows.c" "$source_dir/hw/display/ati_gl.c"
        ;;
    *)
        echo "Unsupported OS X emulator build host: $host" >&2
        exit 1
        ;;
esac
printf '%s\n' "$revision" > "$source_dir/CLASSICMAC-SOURCE-REVISION"

mkdir -p "$build_dir"
cd "$build_dir"
if [ ! -f build.ninja ]; then
    ../configure --target-list=ppc-softmmu --python="${PYTHON_BIN:-python3}" \
        --enable-tcg --enable-slirp --enable-vnc "$crypto" \
        --audio-drv-list="$audio" "$cocoa" --disable-gtk --disable-sdl \
        --disable-curses --disable-docs --disable-guest-agent \
        --disable-install-blobs --disable-debug-info --disable-werror --extra-cflags=-O2
fi
ninja -j "${CLASSICMAC_OSX_JOBS:-$jobs}" "qemu-system-ppc$suffix"
