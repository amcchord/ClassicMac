#!/usr/bin/env python3
"""Stage the Linux runtime; glibc, EGL/GLES and audio drivers belong to the host."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
build, out = map(Path, sys.argv[1:3])
for stale in ('bin', 'lib', 'Browser', 'Licenses', 'qemu', 'qemu-osx', 'qemu-g5', 'Iconfactory'):
    shutil.rmtree(out / stale, ignore_errors=True)
bin_dir = out / 'bin'
lib_dir = out / 'lib'
bin_dir.mkdir(parents=True, exist_ok=True)
lib_dir.mkdir(exist_ok=True)
excluded = (
    'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so', 'libresolv.so', 'ld-linux',
    'libEGL', 'libGLES', 'libGLX', 'libGLdispatch', 'libdrm', 'libgbm', 'libX', 'libxcb',
    'libpulse', 'libasound',
)
seen = set()


def stage_libs(file):
    for line in subprocess.check_output(['ldd', str(file)], text=True).splitlines():
        m = re.search(r'=> (/\S+)', line)
        if not m:
            continue
        path = Path(m[1])
        name = path.name
        if name in seen or name.startswith(excluded):
            continue
        seen.add(name)
        shutil.copyfile(path, lib_dir / name)
        stage_libs(path)


osx = root / 'vendor/qemu-osx'
g5 = root / 'vendor/qemu-g5'
engines = {name: build / name for name in ['qemu-system-m68k', 'qemu-system-ppc', 'qemu-img']}
engines['qemu-system-ppc-osx'] = osx / 'build/qemu-system-ppc'
engines['qemu-system-ppc64'] = g5 / 'build/qemu-system-ppc64'
for name, source in engines.items():
    target = bin_dir / name
    shutil.copyfile(source, target)
    target.chmod(0o755)
    subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN/../lib', str(target)])
    stage_libs(source)

shutil.copytree(root / 'osx/firmware', out / 'qemu-osx', dirs_exist_ok=True)
shutil.copytree(osx / 'pc-bios/keymaps', out / 'qemu-osx/keymaps', dirs_exist_ok=True)
shutil.copytree(root / 'g5/firmware', out / 'qemu-g5', dirs_exist_ok=True)
shutil.copytree(g5 / 'pc-bios/keymaps', out / 'qemu-g5/keymaps', dirs_exist_ok=True)
for path in lib_dir.iterdir():
    subprocess.check_call(['patchelf', '--set-rpath', '$ORIGIN', str(path)])

firmware = out / 'qemu/pc-bios'
firmware.mkdir(parents=True, exist_ok=True)
for name in ['mac_qfb.rom', 'openbios-ppc', 'vgabios-stdvga.bin', 'qemu_vga.ndrv']:
    shutil.copyfile(root / 'vendor/qemu/pc-bios' / name, firmware / name)
shutil.copytree(root / 'vendor/qemu/pc-bios/keymaps', firmware / 'keymaps', dirs_exist_ok=True)
for source, name in [
    ('Resources/Quadra800.rom', 'Quadra800.rom'),
    ('shared/declrom', 'declrom'),
    ('shared/ndrvloader', 'ndrvloader'),
    ('shared/pram-seed.img', 'pram-seed.img'),
]:
    shutil.copyfile(root / source, out / name)

shutil.copytree(root / 'Resources/Iconfactory', out / 'Iconfactory', dirs_exist_ok=True)
licenses = out / 'Licenses'
licenses.mkdir(exist_ok=True)
for source, name in [
    ('vendor/qemu/COPYING', 'QEMU-GPL-2.0.txt'),
    ('vendor/qemu/COPYING.LIB', 'QEMU-LGPL-2.1.txt'),
    ('vendor/qemu/LICENSE', 'QEMU-LICENSE.txt'),
    ('platinum/fonts/LICENSE-fonts.txt', 'Platinum-fonts.txt'),
]:
    shutil.copyfile(root / source, licenses / name)

# Distribution copyright/source notices for copied shared libraries.
original = subprocess.check_output(['ldconfig', '-p'], text=True)
for path in sorted(lib_dir.iterdir()):
    try:
        match = re.search(r'\s' + re.escape(path.name) + r' .* => (/\S+)', original)
        if match:
            package = subprocess.check_output(
                ['dpkg-query', '-S', str(Path(match[1]).resolve())],
                text=True,
            ).split(': ')[0]
            notice = Path('/usr/share/doc') / package.split(':')[0] / 'copyright'
            if notice.exists():
                shutil.copyfile(notice, licenses / (package.replace(':', '-') + '-copyright'))
    except subprocess.CalledProcessError:
        pass

shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', out / 'ThirdPartyNotices.md')
(out / 'runtime-sha256.json').write_text(
    json.dumps(
        {
            str(p.relative_to(out)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(out.rglob('*'))
            if p.is_file() and p.name != 'runtime-sha256.json'
        },
        indent=2,
    ) + '\n'
)
