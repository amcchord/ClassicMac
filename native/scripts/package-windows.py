#!/usr/bin/env python3
"""Bundle the native executable, dynamic Qt libraries and QEMU in one portable EXE."""
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from package_common import copy_msys2_licenses, finish_runtime, read_version

root = Path(__file__).resolve().parents[2]
version = read_version(root)
build = Path(sys.argv[1]).resolve()
prefix = Path(sys.executable).resolve().parents[1]
out = root / 'output/native-package'
app = out / 'app'
if app.exists():
    shutil.rmtree(app)
app.mkdir(parents=True, exist_ok=True)
seen = set()
bundled_prefix_files = []


def stage(source: Path, destination: Path | None = None):
    destination = destination or (app / source.name)
    key = str(destination).lower()
    if key in seen:
        return
    seen.add(key)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    try:
        if source.resolve().is_relative_to(prefix.resolve()):
            bundled_prefix_files.append(source)
    except ValueError:
        pass
    for dll in re.findall(r'DLL Name:\s*(\S+)', subprocess.check_output(['objdump', '-p', str(source)], text=True)):
        candidate = prefix / 'bin' / dll
        if candidate.exists():
            stage(candidate)


stage(build / 'classicmac.exe')
plugin = next(
    (p for p in [prefix / 'share/qt6/plugins', prefix / 'lib/qt6/plugins'] if (p / 'platforms/qwindows.dll').exists()),
    None,
)
if plugin is None:
    raise SystemExit('Qt Windows platform plugin not found')
for group, names in [
    ('platforms', ['qwindows.dll']),
    ('tls', ['qopensslbackend.dll', 'qschannelbackend.dll']),
    ('imageformats', ['qjpeg.dll', 'qico.dll']),
]:
    for name in names:
        source = plugin / group / name
        if source.exists():
            stage(source, app / 'plugins' / group / name)
for pattern in ['libssl-*.dll', 'libcrypto-*.dll']:
    for source in (prefix / 'bin').glob(pattern):
        stage(source)

(app / 'qt.conf').write_text('[Paths]\nPlugins=plugins\n')
shutil.copytree(root / 'output/desktop/runtime', app / 'runtime', dirs_exist_ok=True)

# Deduplicate common DLLs already staged into app/ (which Engine::start prepends
# to PATH for QEMU child processes) before regenerating runtime-sha256.json.
app_dlls = {
    p.name.lower(): p
    for p in app.iterdir()
    if p.is_file() and p.suffix.lower() == '.dll'
}
runtime_bin = app / 'runtime/bin'
if runtime_bin.is_dir():
    for candidate in list(runtime_bin.iterdir()):
        if not candidate.is_file() or candidate.suffix.lower() != '.dll':
            continue
        shared = app_dlls.get(candidate.name.lower())
        if shared is not None and candidate.read_bytes() == shared.read_bytes():
            candidate.unlink()

finish_runtime(root, app / 'runtime')
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', app / 'ThirdPartyNotices.md')

runtime_msys2 = app / 'runtime/Licenses/MSYS2-runtime'
skip_msys2_dirs = {p.name for p in runtime_msys2.iterdir() if p.is_dir()} if runtime_msys2.is_dir() else set()
copy_msys2_licenses(prefix, bundled_prefix_files, app / 'Licenses/MSYS2', skip_dirs=skip_msys2_dirs)

manifest_text = json.dumps(
    {
        str(p.relative_to(app)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(app.rglob('*'))
        if p.is_file() and p.name != 'manifest-sha256.json'
    },
    indent=2,
) + '\n'
(app / 'manifest-sha256.json').write_text(manifest_text)
cache_tag = hashlib.sha256(manifest_text.encode('utf-8')).hexdigest()[:12]

text = (root / 'native/resources/portable.nsi.in').read_text()


def windows_path(p: Path) -> str:
    return str(p).replace('/', chr(92))


exe_name = f'ClassicMac-{version}-windows-x64.exe'
text = (
    text.replace('@VERSION@', version)
    .replace('@CACHE_TAG@', cache_tag)
    .replace('@OUTPUT@', windows_path(out / exe_name))
    .replace('@ICON@', windows_path(root / 'desktop/ui/app.ico'))
    .replace('@APP@', windows_path(app) + chr(92) + '*.*')
)
(out / 'portable.nsi').write_text(text)
subprocess.check_call(['makensis', str(out / 'portable.nsi')], cwd=out)
packages = root / 'output/desktop/packages'
packages.mkdir(parents=True, exist_ok=True)
shutil.copyfile(out / exe_name, packages / exe_name)
