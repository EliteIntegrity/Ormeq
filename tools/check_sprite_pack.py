"""Verify optional sprite pack hashes without requiring artwork in source."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def check(root=ROOT, require_pack=False):
    root=Path(root)
    files=json.loads((root/'lib/tiles-manifest.json').read_text())['files']
    folder=root/'lib/tiles/pack'
    if not folder.exists() and not require_pack:
        print('Sprite artwork absent: glyph fallback available')
        return set()
    paths=set()
    for entry in files:
        name=entry['path']
        if Path(name).name != name or '/' in name or '\\' in name or ':' in name:
            raise ValueError('Invalid sprite inventory path')
        path=folder/name
        data=path.read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Sprite artwork changed: '+name)
        paths.add('lib/tiles/pack/'+name)
    if {p.name for p in folder.iterdir()} != {entry['path'] for entry in files}:
        raise ValueError('Unexpected sprite pack file')
    live={re.sub('[^a-z0-9]+','-',line.rstrip().split(':',2)[2].lower()).strip('-')
          for line in (root/'lib/edit/r_info.txt').read_text(encoding='latin-1').splitlines()
          if line.startswith('N:')}
    for index in folder.glob('monster-*.txt'):
        for line in index.read_text().splitlines():
            if line and not line.startswith('#') and line.split()[0] not in live:
                raise ValueError('Sprite names a retired monster: '+line.split()[0])
    print(f'{len(paths)} optional sprite files verified')
    return paths


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-pack',action='store_true')
    check(require_pack=parser.parse_args().require_pack)
