#!/usr/bin/env bash
# Exact corresponding source for the native GPL helper and its static libraries.
# Run after build-copland.sh. Includes submodule sources; excludes build outputs.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT="${1:-$ROOT_DIR/dist/ClassicMac-Copland-source.tar.gz}"
[ ! -e "$OUTPUT" ] || { echo "Output already exists: $OUTPUT" >&2; exit 1; }
python3 - "$ROOT_DIR" "$OUTPUT" <<'PY'
from pathlib import Path
import io,subprocess,sys,tarfile
root=Path(sys.argv[1]);output=Path(sys.argv[2])
output.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(output,'x:gz') as archive:
    for name in ('dingusppc','SDL2'):
        repo=root/'vendor'/name
        revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'])
        info=tarfile.TarInfo(f'ClassicMac-Copland-source/vendor/{name}/SOURCE-REVISION');info.size=len(revision)
        archive.addfile(info,io.BytesIO(revision))
        tracked=subprocess.check_output(['git','-C',str(repo),'ls-files','--recurse-submodules','-z']).split(b'\0')
        for raw in tracked:
            if not raw: continue
            source=repo/raw.decode()
            if source.is_file():archive.add(source,arcname=f'ClassicMac-Copland-source/vendor/{name}/{raw.decode()}',recursive=False)
    for relative in ('copland','scripts/build-copland.sh','scripts/package-copland-source.sh'):
        archive.add(root/relative,arcname='ClassicMac-Copland-source/'+relative,
                    filter=lambda info: None if '__pycache__' in Path(info.name).parts or info.name.endswith('.pyc') else info)
    # The overlays are untracked in the pinned vendor checkout.
    for relative in ('devices/serial/chario_copland.h','core/classicmac_control.h','devices/common/classicmac_clock.h'):
        archive.add(root/'vendor/dingusppc'/relative,arcname='ClassicMac-Copland-source/vendor/dingusppc/'+relative)
print(output)
PY
