"""Keep the embedded runtime inventory accurate after desktop-only staging."""
from pathlib import Path
import hashlib
import json
import shutil

def finish_runtime(root: Path, runtime: Path):
    shutil.rmtree(runtime / 'Browser', ignore_errors=True)
    shutil.copyfile(root / 'THIRD_PARTY_NOTICES.md', runtime / 'ThirdPartyNotices.md')
    files = {str(p.relative_to(runtime)).replace('\\','/'): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(runtime.rglob('*'))
             if p.is_file() and p.name != 'runtime-sha256.json'}
    (runtime / 'runtime-sha256.json').write_text(json.dumps(files, indent=2)+'\n')
