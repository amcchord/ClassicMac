#!/usr/bin/env bash
# Build the PowerPC Mac OS X engine separately from ClassicMac's QEMU 11.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$root/vendor/qemu-osx"
build_dir="$source_dir/build"
revision=98f88efe8f07067667b8d9b1a5ce9551d34036c1
repository=https://github.com/qemu/qemu.git

host="$(uname -s)"
patches=("$root/osx/retroboot-qemu10.patch")
extra_cflags=-O2
extra_configure_args=()
case "$host" in
    Darwin)
        export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}"
        crypto=--disable-gcrypt
        audio=coreaudio
        cocoa=--enable-cocoa
        jobs="$(sysctl -n hw.ncpu)"
        suffix=
        patches+=(
            "$root/osx/coreaudio-device-format-qemu10.patch"
            "$root/osx/classicmac-display-qemu10.patch"
        )
        extra_cflags="-O2 -ffp-contract=off"
        extra_configure_args=(
            --disable-capstone
            --disable-gnutls
            --disable-nettle
            --disable-libssh
            --disable-libusb
            --disable-curl
            --disable-bzip2
            --disable-lzo
            --disable-snappy
            --disable-vnc-sasl
            --disable-vnc-jpeg
        )
        ;;
    Linux)
        crypto=--enable-gcrypt
        audio=pa,alsa
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
        ;;
    *)
        echo "Unsupported OS X emulator build host: $host" >&2
        exit 1
        ;;
esac

sha256_text() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | awk '{print $1}'
    else
        shasum -a 256 | awk '{print $1}'
    fi
}

sha256_files() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$@" | awk '{print $1}'
    else
        shasum -a 256 "$@" | awk '{print $1}'
    fi
}

configure_args=(
    --target-list=ppc-softmmu
    --python="${PYTHON_BIN:-python3}"
    --enable-tcg
    --enable-slirp
    --enable-vnc
    "$crypto"
    --audio-drv-list="$audio"
    "$cocoa"
    --disable-gtk
    --disable-sdl
    --disable-curses
    --disable-docs
    --disable-guest-agent
    --disable-install-blobs
    --disable-debug-info
    --disable-werror
    --extra-cflags="$extra_cflags"
    "${extra_configure_args[@]}"
)

patch_inputs=("${patches[@]}")
case "$host" in
    MINGW*|MSYS*)
        patch_inputs+=("$root/osx/ati-gl-windows.c")
        ;;
esac
patch_stamp="$({
    printf 'revision=%s\nhost=%s\n' "$revision" "$host"
    sha256_files "${patch_inputs[@]}"
} | sha256_text)"
config_stamp="$({
    printf 'revision=%s\nhost=%s\ndeployment_target=%s\n' \
        "$revision" "$host" "${MACOSX_DEPLOYMENT_TARGET:-}"
    printf '%s\n' "${configure_args[@]}"
} | sha256_text)"

patch_stamp_file="$source_dir/CLASSICMAC-OSX-PATCHES.sha256"
config_stamp_file="$build_dir/CLASSICMAC-OSX-CONFIGURE.sha256"
old_patch_stamp="$(cat "$patch_stamp_file" 2>/dev/null || true)"
old_config_stamp="$(cat "$config_stamp_file" 2>/dev/null || true)"

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
    head_revision="$(git -C "$source_dir" rev-parse HEAD)"
    if [ "$head_revision" != "$revision" ]; then
        git -C "$source_dir" checkout -f --detach "$revision"
    fi

    need_repatch=0
    if [ "$head_revision" != "$revision" ] || [ "$old_patch_stamp" != "$patch_stamp" ]; then
        need_repatch=1
    elif [ -n "$old_config_stamp" ] || [ -f "$build_dir/build.ninja" ]; then
        if [ "$old_config_stamp" != "$config_stamp" ]; then
            need_repatch=1
        fi
    fi

    if [ "$need_repatch" = 1 ]; then
        git -C "$source_dir" reset --hard "$revision"
        git -C "$source_dir" clean -fd -e build
        rm -f "$config_stamp_file"
        for p in "${patches[@]}"; do
            git -C "$source_dir" apply --check "$p"
            git -C "$source_dir" apply "$p"
        done
        case "$host" in
            MINGW*|MSYS*)
                cp "$root/osx/ati-gl-windows.c" "$source_dir/hw/display/ati_gl.c"
                ;;
        esac
        printf '%s\n' "$patch_stamp" > "$patch_stamp_file"
        old_config_stamp=
    fi
else
    # Release source archives contain the already patched tree.
    [ "$(cat "$source_dir/CLASSICMAC-SOURCE-REVISION")" = "$revision" ]
    case "$host" in
        MINGW*|MSYS*)
            if ! cmp -s "$root/osx/ati-gl-windows.c" "$source_dir/hw/display/ati_gl.c"; then
                cp "$root/osx/ati-gl-windows.c" "$source_dir/hw/display/ati_gl.c"
            fi
            ;;
    esac
fi

printf '%s\n' "$revision" > "$source_dir/CLASSICMAC-SOURCE-REVISION"
if [ "${CLASSICMAC_PREPARE_ONLY:-0}" = 1 ]; then
    exit 0
fi

mkdir -p "$build_dir"
cd "$build_dir"
if [ ! -f build.ninja ] || [ "$old_config_stamp" != "$config_stamp" ]; then
    ../configure "${configure_args[@]}"
    printf '%s\n' "$config_stamp" > "$config_stamp_file"
fi
ninja -j "${CLASSICMAC_OSX_JOBS:-$jobs}" "qemu-system-ppc$suffix"
