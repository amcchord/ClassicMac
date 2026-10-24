#!/usr/bin/env python3
"""Bundle the native executable, dynamic Qt libraries and QEMU in one portable EXE."""
import hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
build=Path(sys.argv[1]).resolve(); prefix=Path(sys.executable).resolve().parents[1]
out=root/'output/native-package'; app=out/'app'; app.mkdir(parents=True,exist_ok=True)
seen=set()
def stage(source,destination=None):
    destination=destination or app/source.name
    key=str(destination).lower()
    if key in seen:return
    seen.add(key);destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    for dll in re.findall(r'DLL Name:\s*(\S+)',subprocess.check_output(['objdump','-p',str(source)],text=True)):
        candidate=prefix/'bin'/dll
        if candidate.exists():stage(candidate)
stage(build/'classicmac.exe')
plugin=next((p for p in [prefix/'share/qt6/plugins',prefix/'lib/qt6/plugins'] if (p/'platforms/qwindows.dll').exists()),None)
if plugin is None:raise SystemExit('Qt Windows platform plugin not found')
for group,names in [('platforms',['qwindows.dll']),('tls',['qopensslbackend.dll','qschannelbackend.dll']),('imageformats',['qjpeg.dll','qico.dll'])]:
    for name in names:
        source=plugin/group/name
        if source.exists():stage(source,app/'plugins'/group/name)
for pattern in ['libssl-*.dll','libcrypto-*.dll']:
    for source in (prefix/'bin').glob(pattern):stage(source)
(app/'qt.conf').write_text('[Paths]\nPlugins=plugins\n')
shutil.copytree(root/'output/desktop/runtime',app/'runtime',dirs_exist_ok=True)
shutil.rmtree(app/'runtime/Browser',ignore_errors=True)
shutil.copyfile(root/'THIRD_PARTY_NOTICES.md',app/'ThirdPartyNotices.md')
shutil.copytree(prefix/'share/licenses',app/'Licenses/MSYS2',dirs_exist_ok=True)
(app/'manifest-sha256.json').write_text(json.dumps({str(p.relative_to(app)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in app.rglob('*') if p.is_file() and p.name!='manifest-sha256.json'},indent=2)+'\n')
text='''Unicode true
!include "FileFunc.nsh"
Name "ClassicMac"
OutFile "@OUTPUT@"
RequestExecutionLevel user
SilentInstall silent
SetCompressor /SOLID lzma
VIProductVersion "4.6.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "ClassicMac"
VIAddVersionKey /LANG=1033 "FileDescription" "ClassicMac native portable application"
VIAddVersionKey /LANG=1033 "FileVersion" "4.6.0"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Austin McChord"
Icon "@ICON@"
Section
  InitPluginsDir
  SetOutPath "$PLUGINSDIR/ClassicMac"
  File /r "@APP@\\*.*"
  ${GetParameters} $R0
  ${GetOptions} $R0 "--extract=" $R1
  StrCmp $R1 "" run
  CreateDirectory "$R1"
  CopyFiles /SILENT "$PLUGINSDIR/ClassicMac/*" "$R1"
  Goto done
run:
  ExecWait '"$PLUGINSDIR/ClassicMac/classicmac.exe" $R0' $R2
  SetErrorLevel $R2
done:
SectionEnd
'''
text=text.replace('@OUTPUT@',str(out/'ClassicMac-4.6.0-windows-x64.exe')).replace('@ICON@',str(root/'desktop/ui/app.ico')).replace('@APP@',str(app))
(out/'portable.nsi').write_text(text)
subprocess.check_call(['makensis',str(out/'portable.nsi')],cwd=out)
packages=root/'output/desktop/packages';packages.mkdir(parents=True,exist_ok=True)
shutil.copyfile(out/'ClassicMac-4.6.0-windows-x64.exe',packages/'ClassicMac-4.6.0-windows-x64.exe')
