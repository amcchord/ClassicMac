#!/usr/bin/env python3
"""Stage the ELF frontend, its dynamic Qt closure and the engines as an AppImage."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from package_common import finish_runtime, read_version

root = Path(__file__).resolve().parents[2]
version = read_version(root)
build = Path(sys.argv[1]).resolve()
out = root / 'output/native-package'
app = out / 'ClassicMac.AppDir'
if app.exists():
    shutil.rmtree(app)
app.mkdir(parents=True, exist_ok=True)
lib = app / 'lib'
lib.mkdir(exist_ok=True)

# Exclude host glibc/runtime, GPU/Mesa/DRI/X11/Wayland, audio, C++ runtime,
# D-Bus/systemd, and font stack libraries so the AppImage integrates cleanly
# with the host distribution's drivers and window system.
SKIP = (
    'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libresolv.so', 'ld-linux',
    'libEGL', 'libGLES', 'libGL', 'libGLX', 'libGLdispatch', 'libdrm', 'libgbm',
    'libX11', 'libXau', 'libXdmcp', 'libXext', 'libXfixes', 'libXi', 'libXrender', 'libxcb',
    'libwayland-client', 'libwayland-cursor', 'libwayland-egl',
    'libpulse', 'libasound',
    'libstdc++', 'libgcc_s',
    'libdbus-1', 'libsystemd',
    'libfontconfig', 'libfreetype',
)

seen = set()


def deps(source: Path):
    for path in re.findall(r'=> (/\S+)', subprocess.check_output(['ldd', str(source)], text=True)):
        f = Path(path)
        if f.name in seen or f.name.startswith(SKIP):
            continue
        seen.add(f.name)
        target = lib / f.name
        shutil.copyfile(f, target)
        target.chmod(0o755)
        deps(f)


shutil.copyfile(build / 'classicmac', app / 'classicmac')
(app / 'classicmac').chmod(0o755)
deps(build / 'classicmac')

# Qt's OpenSSL backend dlopens libssl, so ldd does not report it. Bundle the
# matching pair; a newer host libssl cannot use this build's older libcrypto.
listing = subprocess.check_output(['ldconfig', '-p'], text=True)
for soname in ['libssl.so.3', 'libcrypto.so.3']:
    match = re.search(r'\s' + re.escape(soname) + r' .* => (/\S+)', listing)
    if not match:
        raise SystemExit('Missing TLS runtime ' + soname)
    source = Path(match[1])
    if soname not in seen:
        seen.add(soname)
        target = lib / soname
        shutil.copyfile(source, target)
        target.chmod(0o755)
        deps(source)

plugin = Path(subprocess.check_output(['qtpaths6', '--query', 'QT_INSTALL_PLUGINS'], text=True).strip())
for group in [
    'platforms', 'platforminputcontexts', 'xcbglintegrations', 'tls', 'imageformats',
    'wayland-shell-integration', 'wayland-decoration-client', 'wayland-graphics-integration-client',
]:
    for source in (plugin / group).glob('*.so'):
        if 'eglfs' in source.name or 'vnc' in source.name or 'linuxfb' in source.name:
            continue
        target = app / 'plugins' / group / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o755)
        deps(source)

subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN/lib', str(app / 'classicmac')])
for bundled_lib in lib.iterdir():
    if bundled_lib.is_file():
        subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN', str(bundled_lib)])
for bundled_plugin in (app / 'plugins').rglob('*.so'):
    subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN/../../lib', str(bundled_plugin)])

(app / 'qt.conf').write_text('[Paths]\nPlugins=plugins\n')
shutil.copytree(root / 'output/desktop/runtime', app / 'runtime', dirs_exist_ok=True)
finish_runtime(root, app / 'runtime')
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', app / 'ThirdPartyNotices.md')

notices = app / 'Licenses'
notices.mkdir(exist_ok=True)
for f in sorted(lib.iterdir()):
    match = re.search(r'\s' + re.escape(f.name) + r' .* => (/\S+)', listing)
    if not match:
        continue
    try:
        package = subprocess.check_output(
            ['dpkg-query', '-S', str(Path(match[1]).resolve())],
            text=True, stderr=subprocess.DEVNULL,
        ).split(': ')[0]
        notice = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
        if notice.exists():
            shutil.copyfile(notice, notices / (package.replace(':', '-') + '-copyright'))
    except subprocess.CalledProcessError:
        pass

(app / 'AppRun').write_text(
    '#!/bin/sh\n'
    'APPDIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\n'
    'if [ -z "${CLASSICMAC_ORIG_LD_LIBRARY_PATH+x}" ]; then\n'
    '  export CLASSICMAC_ORIG_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"\n'
    'fi\n'
    'if [ -z "${CLASSICMAC_ORIG_QT_PLUGIN_PATH+x}" ]; then\n'
    '  export CLASSICMAC_ORIG_QT_PLUGIN_PATH="${QT_PLUGIN_PATH:-}"\n'
    'fi\n'
    'export QT_PLUGIN_PATH="$APPDIR/plugins"\n'
    'exec "$APPDIR/classicmac" "$@"\n'
)
(app / 'AppRun').chmod(0o755)
(app / 'classicmac.desktop').write_text(
    '[Desktop Entry]\n'
    'Type=Application\n'
    'Name=ClassicMac\n'
    'Comment=Run classic Macintosh systems\n'
    'Exec=classicmac\n'
    'Icon=classicmac\n'
    'Terminal=false\n'
    'StartupWMClass=classicmac\n'
    'Categories=Utility;\n'
)
shutil.copyfile(root / 'desktop/ui/app.png', app / 'classicmac.png')
(app / 'manifest-sha256.json').write_text(
    json.dumps(
        {
            str(p.relative_to(app)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(app.rglob('*'))
            if p.is_file() and p.name != 'manifest-sha256.json'
        },
        indent=2,
    ) + '\n'
)

tool = out / 'appimagetool.AppImage'
digest = 'ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0'
if not tool.exists():
    urllib.request.urlretrieve(
        'https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage',
        tool,
    )
if hashlib.sha256(tool.read_bytes()).hexdigest() != digest:
    raise SystemExit('AppImage tool checksum mismatch')
tool.chmod(0o755)
packages = root / 'output/desktop/packages'
packages.mkdir(parents=True, exist_ok=True)
env = os.environ | {'ARCH': 'x86_64', 'APPIMAGE_EXTRACT_AND_RUN': '1'}
subprocess.check_call(
    [str(tool), '--no-appstream', str(app), str(packages / f'ClassicMac-{version}-linux-x64.AppImage')],
    env=env,
)
