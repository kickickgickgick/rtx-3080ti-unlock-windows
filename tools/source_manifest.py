"""Record the explicit Git-tracked source set used for a source/USB release."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
git = ['git', '-c', f'safe.directory={root.as_posix()}', '-C', str(root)]
top = Path(subprocess.check_output(git + ['rev-parse', '--show-toplevel'], text=True).strip()).resolve()
if top != root:
    raise SystemExit('Run from this project Git checkout.')
names = subprocess.check_output(git + ['ls-files', '-z']).decode().rstrip('\0').split('\0')
records = []
for name in sorted(names):
    if not name or name == 'SOURCE-MANIFEST.json':
        continue
    path = root / name
    if path.is_symlink() or root not in path.resolve().parents or not path.is_file():
        raise SystemExit('Invalid tracked source path: ' + name)
    records.append({'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
if not records:
    raise SystemExit('No tracked files; stage the source files first.')
payload = json.dumps({'schema': 1, 'files': records}, indent=2) + '\n'
(root / 'SOURCE-MANIFEST.json').write_bytes(payload.encode('utf-8'))
print('Recorded %d source files.' % len(records))
