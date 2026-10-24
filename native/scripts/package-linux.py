#!/usr/bin/env python3
"""Stage the ELF frontend, its dynamic Qt closure and the engines as an AppImage."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,urllib.request
from package_common import finish_runtime
root=Path(__file__).resolve().parents[2];build=Path(sys.argv[1]).resolve()
out=root/'output/native-package';app=out/'ClassicMac.AppDir';app.mkdir(parents=True,exist_ok=True)
lib=app/'lib';lib.mkdir(exist_ok=True);seen=set()
def deps(source):
    for path in re.findall(r'=> (/\S+)',subprocess.check_output(['ldd',str(source)],text=True)):
        f=Path(path)
        if f.name in seen or f.name.startswith(('libc.so','libm.so','libdl.so','libpthread.so','librt.so','libresolv.so','ld-linux')):continue
        seen.add(f.name);shutil.copyfile(f,lib/f.name);deps(f)
shutil.copyfile(build/'classicmac',app/'classicmac');(app/'classicmac').chmod(0o755);deps(build/'classicmac')
# Qt's OpenSSL backend dlopens libssl, so ldd does not report it. Bundle the
# matching pair; a newer host libssl cannot use this build's older libcrypto.
listing=subprocess.check_output(['ldconfig','-p'],text=True)
for soname in ['libssl.so.3','libcrypto.so.3']:
    match=re.search(r'\s'+re.escape(soname)+r' .* => (/\S+)',listing)
    if not match:raise SystemExit('Missing TLS runtime '+soname)
    source=Path(match[1]);shutil.copyfile(source,lib/soname);seen.add(soname);deps(source)
plugin=Path(subprocess.check_output(['qtpaths6','--query','QT_INSTALL_PLUGINS'],text=True).strip())
for group in ['platforms','platforminputcontexts','xcbglintegrations','tls','imageformats','wayland-shell-integration','wayland-decoration-client','wayland-graphics-integration-client']:
    for source in (plugin/group).glob('*.so'):
        if 'eglfs' in source.name or 'vnc' in source.name or 'linuxfb' in source.name:continue
        target=app/'plugins'/group/source.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target);deps(source)
(app/'qt.conf').write_text('[Paths]\nPlugins=plugins\n')
shutil.copytree(root/'output/desktop/runtime',app/'runtime',dirs_exist_ok=True);finish_runtime(root,app/'runtime')
shutil.copyfile(root/'THIRD_PARTY_NOTICES.md',app/'ThirdPartyNotices.md')
notices=app/'Licenses';notices.mkdir(exist_ok=True)
for f in lib.iterdir():
    listing=subprocess.check_output(['ldconfig','-p'],text=True);match=re.search(r'\s'+re.escape(f.name)+r' .* => (/\S+)',listing)
    if not match:continue
    try:
        package=subprocess.check_output(['dpkg-query','-S',str(Path(match[1]).resolve())],text=True,stderr=subprocess.DEVNULL).split(': ')[0]
        notice=Path('/usr/share/doc')/package.split(':')[0]/'copyright'
        if notice.exists():shutil.copyfile(notice,notices/(package.replace(':','-')+'-copyright'))
    except subprocess.CalledProcessError:pass
(app/'AppRun').write_text('#!/bin/sh\nAPPDIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\nexport LD_LIBRARY_PATH="$APPDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexport QT_PLUGIN_PATH="$APPDIR/plugins"\nexec "$APPDIR/classicmac" "$@"\n');(app/'AppRun').chmod(0o755)
(app/'classicmac.desktop').write_text('[Desktop Entry]\nType=Application\nName=ClassicMac\nExec=classicmac\nIcon=classicmac\nCategories=Utility;\n')
shutil.copyfile(root/'desktop/ui/app.png',app/'classicmac.png')
(app/'manifest-sha256.json').write_text(json.dumps({str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest() for p in app.rglob('*') if p.is_file() and p.name!='manifest-sha256.json'},indent=2)+'\n')
tool=out/'appimagetool.AppImage';digest='95cbe7cce9717fce90c484e34052ee7c7f1d7635b33c12525b4776826a7d29b6'
if not tool.exists():urllib.request.urlretrieve('https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage',tool)
if hashlib.sha256(tool.read_bytes()).hexdigest()!=digest:raise SystemExit('AppImage tool checksum mismatch')
tool.chmod(0o755);packages=root/'output/desktop/packages';packages.mkdir(parents=True,exist_ok=True)
env=os.environ|{'ARCH':'x86_64','APPIMAGE_EXTRACT_AND_RUN':'1'}
subprocess.check_call([str(tool),'--no-appstream',str(app),str(packages/'ClassicMac-4.6.1-linux-x64.AppImage')],env=env)
