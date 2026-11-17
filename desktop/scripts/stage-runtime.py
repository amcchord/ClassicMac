#!/usr/bin/env python3
"""Bundle only the Windows runtime's actual DLL dependency closure and notices."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
build, out = map(Path, sys.argv[1:3])
binary = out / 'bin'
binary.mkdir(parents=True, exist_ok=True)
mingw = Path(sys.executable).resolve().parents[1]
seen = set()
def stage(file, name=None):
    name = name or file.name
    key = name.lower()
    if key in seen: return
    seen.add(key)
    shutil.copyfile(file, binary / name)
    imports = subprocess.check_output(['objdump', '-p', str(file)], text=True)
    for dll in re.findall(r'DLL Name:\s*(\S+)', imports):
        candidate = mingw / 'bin' / dll
        if candidate.is_file(): stage(candidate)
for name in ('qemu-system-m68k.exe', 'qemu-system-ppc.exe', 'qemu-img.exe'):
    stage(build / name)
osx = root / 'vendor/qemu-osx'
stage(osx / 'build/qemu-system-ppc.exe', 'qemu-system-ppc-osx.exe')
shutil.copytree(root / 'osx/firmware', out / 'qemu-osx', dirs_exist_ok=True)
shutil.copytree(osx / 'pc-bios/keymaps', out / 'qemu-osx/keymaps', dirs_exist_ok=True)
firmware = out / 'qemu/pc-bios'
firmware.mkdir(parents=True, exist_ok=True)
for name in ('mac_qfb.rom', 'openbios-ppc', 'vgabios-stdvga.bin', 'qemu_vga.ndrv'):
    shutil.copyfile(root / 'vendor/qemu/pc-bios' / name, firmware / name)
shutil.copytree(root / 'vendor/qemu/pc-bios/keymaps', firmware / 'keymaps', dirs_exist_ok=True)
for source, name in [('Resources/Quadra800.rom', 'Quadra800.rom'), ('shared/declrom', 'declrom'), ('shared/ndrvloader', 'ndrvloader'), ('shared/pram-seed.img', 'pram-seed.img')]:
    shutil.copyfile(root / source, out / name)
shutil.copytree(root / 'browser', out / 'Browser', dirs_exist_ok=True)
shutil.copytree(root / 'Resources/Iconfactory', out / 'Iconfactory', dirs_exist_ok=True)
shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', out / 'ThirdPartyNotices.md')
licenses = out / 'Licenses'
licenses.mkdir(exist_ok=True)
for source, name in [('vendor/qemu/COPYING', 'QEMU-GPL-2.0.txt'), ('vendor/qemu/COPYING.LIB', 'QEMU-LGPL-2.1.txt'), ('vendor/qemu/LICENSE', 'QEMU-LICENSE.txt'), ('browser/novnc/LICENSE.txt', 'noVNC-MPL-2.0.txt'), ('browser/novnc/vendor/pako/LICENSE', 'pako-MIT.txt'), ('platinum/fonts/LICENSE-fonts.txt', 'Platinum-fonts.txt')]:
    shutil.copyfile(root / source, licenses / name)
shutil.copytree(mingw / 'share/licenses', licenses / 'MSYS2-runtime', dirs_exist_ok=True)
manifest = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}
(out / 'runtime-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
