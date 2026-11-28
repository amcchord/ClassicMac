#!/usr/bin/env python3
"""Bundle only the Windows runtime's actual DLL dependency closure and notices."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'native/scripts'))
from package_common import copy_msys2_licenses

build, out = map(Path, sys.argv[1:3])
for stale in ('bin', 'Browser', 'Licenses', 'qemu', 'qemu-osx', 'qemu-g5', 'Iconfactory'):
    shutil.rmtree(out / stale, ignore_errors=True)
binary = out / 'bin'
binary.mkdir(parents=True, exist_ok=True)
mingw = Path(sys.executable).resolve().parents[1]
seen = set()
bundled_mingw_files = []


def stage(file, name=None):
    name = name or file.name
    key = name.lower()
    if key in seen:
        return
    seen.add(key)
    shutil.copyfile(file, binary / name)
    try:
        if file.resolve().is_relative_to(mingw.resolve()):
            bundled_mingw_files.append(file)
    except ValueError:
        pass
    imports = subprocess.check_output(['objdump', '-p', str(file)], text=True)
    for dll in re.findall(r'DLL Name:\s*(\S+)', imports):
        candidate = mingw / 'bin' / dll
        if candidate.is_file():
            stage(candidate)


for name in ('qemu-system-m68k.exe', 'qemu-system-ppc.exe', 'qemu-img.exe'):
    stage(build / name)
osx = root / 'vendor/qemu-osx'
stage(osx / 'build/qemu-system-ppc.exe', 'qemu-system-ppc-osx.exe')
g5 = root / 'vendor/qemu-g5'
stage(g5 / 'build/qemu-system-ppc64.exe', 'qemu-system-ppc64.exe')
shutil.copytree(root / 'osx/firmware', out / 'qemu-osx', dirs_exist_ok=True)
shutil.copytree(osx / 'pc-bios/keymaps', out / 'qemu-osx/keymaps', dirs_exist_ok=True)
shutil.copytree(root / 'g5/firmware', out / 'qemu-g5', dirs_exist_ok=True)
shutil.copytree(g5 / 'pc-bios/keymaps', out / 'qemu-g5/keymaps', dirs_exist_ok=True)
firmware = out / 'qemu/pc-bios'
firmware.mkdir(parents=True, exist_ok=True)
for name in ('mac_qfb.rom', 'openbios-ppc', 'vgabios-stdvga.bin', 'qemu_vga.ndrv'):
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
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', out / 'ThirdPartyNotices.md')
licenses = out / 'Licenses'
licenses.mkdir(exist_ok=True)
for source, name in [
    ('vendor/qemu/COPYING', 'QEMU-GPL-2.0.txt'),
    ('vendor/qemu/COPYING.LIB', 'QEMU-LGPL-2.1.txt'),
    ('vendor/qemu/LICENSE', 'QEMU-LICENSE.txt'),
    ('platinum/fonts/LICENSE-fonts.txt', 'Platinum-fonts.txt'),
]:
    shutil.copyfile(root / source, licenses / name)
copy_msys2_licenses(mingw, bundled_mingw_files, licenses / 'MSYS2-runtime')
manifest = {
    str(p.relative_to(out)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in sorted(out.rglob('*'))
    if p.is_file() and p.name != 'runtime-sha256.json'
}
(out / 'runtime-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
