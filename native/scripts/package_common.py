"""Shared packaging helpers for native desktop distributions."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess

DLL_LICENSE_ALIASES = {
    'libgcc_s': ('gcc-libs', 'gcc'),
    'libstdc++': ('gcc-libs', 'gcc'),
    'libgomp': ('gcc-libs', 'gcc'),
    'libatomic': ('gcc-libs', 'gcc'),
    'libquadmath': ('gcc-libs', 'gcc'),
    'libwinpthread': ('winpthreads', 'libwinpthread'),
    'libssl': ('openssl',),
    'libcrypto': ('openssl',),
    'qt6': ('qt6-base', 'qt6'),
    'qwindows': ('qt6-base',),
    'qopensslbackend': ('qt6-base',),
    'qschannelbackend': ('qt6-base',),
    'qjpeg': ('qt6-base',),
    'qico': ('qt6-base',),
    'libintl': ('gettext', 'gettext-runtime'),
    'libasprintf': ('gettext',),
    'libiconv': ('libiconv', 'iconv'),
    'libcharset': ('libiconv', 'iconv'),
    'libicu': ('icu',),
    'zlib1': ('zlib',),
    'libz': ('zlib',),
    'libbz2': ('bzip2',),
    'libbrotli': ('brotli',),
    'libjpeg': ('libjpeg-turbo',),
    'libpng': ('libpng',),
    'libpcre2': ('pcre2',),
    'libglib': ('glib2',),
    'libgio': ('glib2',),
    'libgobject': ('glib2',),
    'libgmodule': ('glib2',),
    'libgthread': ('glib2',),
    'libffi': ('libffi',),
    'libfdt': ('dtc',),
    'libhogweed': ('nettle',),
    'libnettle': ('nettle',),
    'libgmp': ('gmp',),
    'libtasn1': ('libtasn1',),
    'libunistring': ('libunistring',),
    'libidn2': ('libidn2',),
    'libpsl': ('libpsl',),
    'libnghttp2': ('nghttp2',),
    'libnghttp3': ('nghttp3',),
    'libngtcp2': ('ngtcp2',),
    'libp11-kit': ('p11-kit',),
    'libgnutls': ('gnutls',),
    'libcurl': ('curl',),
    'libssh2': ('libssh2',),
    'libzstd': ('zstd',),
    'libslirp': ('libslirp',),
    'libharfbuzz': ('harfbuzz',),
    'libfreetype': ('freetype',),
    'libgraphite2': ('graphite2',),
    'libdouble-conversion': ('double-conversion',),
    'libmd4c': ('md4c',),
    'libb2': ('libb2',),
    'libpixman': ('pixman',),
    'libgpg-error': ('libgpg-error',),
    'libgcrypt': ('libgcrypt',),
}

_MSYS2_PKG_PREFIXES = (
    'mingw-w64-ucrt-x86_64-',
    'mingw-w64-clang-x86_64-',
    'mingw-w64-clang-aarch64-',
    'mingw-w64-x86_64-',
    'mingw-w64-i686-',
)


def read_version(root: Path) -> str:
    """Read the canonical release version from the repository VERSION file."""
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if not version:
        raise SystemExit('VERSION file is empty')
    return version


def finish_runtime(root: Path, runtime: Path):
    """Keep the embedded runtime inventory accurate after desktop-only staging."""
    shutil.rmtree(runtime / 'Browser', ignore_errors=True)
    (runtime / 'qemu/pc-bios').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / 'ppcvid/qemu_vga.ndrv', runtime / 'qemu/pc-bios/qemu_vga.ndrv')
    shutil.copyfile(root / 'qfb/mac_qfb.rom', runtime / 'qemu/pc-bios/mac_qfb.rom')
    shutil.copyfile(root / 'shared/declrom', runtime / 'declrom')
    shutil.copyfile(root / 'shared/ndrvloader', runtime / 'ndrvloader')
    shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', runtime / 'ThirdPartyNotices.md')
    files = {
        str(p.relative_to(runtime)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(runtime.rglob('*'))
        if p.is_file() and p.name != 'runtime-sha256.json'
    }
    (runtime / 'runtime-sha256.json').write_text(json.dumps(files, indent=2) + '\n')


def _strip_msys2_prefix(package: str) -> str:
    for prefix in _MSYS2_PKG_PREFIXES:
        if package.startswith(prefix):
            return package[len(prefix):]
    return package


def _copy_license_dir(src_dir: Path, dest_dir: Path, skip: set[str], copied: set[str]):
    if src_dir.name in skip or not src_dir.is_dir():
        return
    shutil.copytree(src_dir, dest_dir / src_dir.name, dirs_exist_ok=True)
    copied.add(src_dir.name)


def copy_msys2_licenses(
    prefix: Path,
    bundled_files: list[Path],
    dest_dir: Path,
    skip_dirs: set[str] | None = None,
) -> set[str]:
    """Copy MSYS2 license notices only for packages owning bundled DLLs/EXEs."""
    licenses_root = prefix / 'share/licenses'
    if not licenses_root.is_dir():
        return set()
    skip = set(skip_dirs or ())
    copied: set[str] = set()
    unique_files = sorted({Path(p) for p in bundled_files if Path(p).is_file()}, key=str)
    if not unique_files:
        return copied

    license_dirs = {d.name.lower(): d for d in licenses_root.iterdir() if d.is_dir()}
    resolved_files: set[str] = set()
    packages: set[str] = set()

    pacman = shutil.which('pacman')
    if pacman:
        query_paths = [str(p) for p in unique_files]
        cygpath = shutil.which('cygpath')
        if cygpath:
            conv = subprocess.run([cygpath, '-u', '-f', '-'],
                                  input='\n'.join(query_paths) + '\n',
                                  text=True, capture_output=True, check=False)
            if conv.returncode == 0:
                converted = [line.strip() for line in conv.stdout.splitlines() if line.strip()]
                if len(converted) == len(query_paths):
                    query_paths = converted
        qo = subprocess.run([pacman, '-Qo', *query_paths],
                            text=True, capture_output=True, check=False)
        for line in qo.stdout.splitlines():
            match = re.match(r'^(.+?)\s+is owned by\s+(\S+)\s+', line)
            if match:
                resolved_files.add(Path(match.group(1)).name.lower())
                packages.add(match.group(2))
        if packages:
            packages_with_licenses: set[str] = set()
            ql = subprocess.run([pacman, '-Ql', *sorted(packages)],
                                text=True, capture_output=True, check=False)
            if ql.returncode == 0:
                for line in ql.stdout.splitlines():
                    pkg, _, filepath = line.partition(' ')
                    if '/share/licenses/' not in filepath:
                        continue
                    rel = filepath.split('/share/licenses/', 1)[1].lstrip('/')
                    if not rel or rel.endswith('/'):
                        continue
                    pkg_dir = rel.split('/', 1)[0]
                    if pkg_dir in skip:
                        packages_with_licenses.add(pkg)
                        continue
                    src = licenses_root / rel
                    if src.is_file():
                        target = dest_dir / rel
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(src, target)
                        copied.add(pkg_dir)
                        packages_with_licenses.add(pkg)
            for pkg in sorted(packages - packages_with_licenses):
                stem = _strip_msys2_prefix(pkg).lower()
                if stem in license_dirs:
                    _copy_license_dir(license_dirs[stem], dest_dir, skip, copied)

    for path in unique_files:
        if path.name.lower() in resolved_files:
            continue
        stem = path.stem.lower()
        candidates: list[str] = []
        for alias_prefix, targets in DLL_LICENSE_ALIASES.items():
            if stem.startswith(alias_prefix):
                candidates.extend(targets)
        normalized = re.sub(r'-(?:x64|seh|sjlj|dw2)$', '', stem)
        while True:
            stripped = re.sub(r'-[\d.]+$', '', normalized)
            if stripped == normalized:
                break
            normalized = stripped
        candidates.extend([normalized, normalized.removeprefix('lib')])
        for candidate in candidates:
            if candidate in license_dirs:
                _copy_license_dir(license_dirs[candidate], dest_dir, skip, copied)
                break
        else:
            bare = normalized.removeprefix('lib')
            for dir_name, dir_path in license_dirs.items():
                if bare and (dir_name == bare or dir_name.startswith(bare + '-')):
                    _copy_license_dir(dir_path, dest_dir, skip, copied)
    return copied
