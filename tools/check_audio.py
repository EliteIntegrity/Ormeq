"""Validate public cue coverage and optional packaged PCM, without requiring assets.

Use --require-pack when preparing the playable release.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import wave

ROOT = Path(__file__).resolve().parent.parent


def check_music(root=ROOT, require_pack=False):
    manifest = json.loads((root / 'lib/xtra/music/pack-manifest.json').read_text())
    entries = manifest['files']
    assert manifest['format'] == 1 and len(entries) <= 1, 'Title music inventory'
    pack = root / 'lib/xtra/music/pack'
    found = 0
    for entry in entries:
        assert entry['path'] == 'title.wav', 'Unexpected title track path'
        assert re.fullmatch('[0-9a-f]{64}', entry['sha256']), 'Title track hash'
        assert 0 < entry['bytes'] <= 128 * 1024 * 1024, 'Title track size'
        path = pack / entry['path']
        if not path.is_file():
            assert not require_pack, 'Missing packaged title music'
            continue
        data = path.read_bytes()
        assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'], path
        with wave.open(str(path), 'rb') as wav:
            assert wav.getframerate() in (44100, 48000) and wav.getsampwidth() == 2, path
            assert wav.getnchannels() in (1, 2) and wav.getcomptype() == 'NONE', path
            assert 0 < wav.getnframes() <= wav.getframerate() * 600, path
            assert len(wav.readframes(wav.getnframes())) == wav.getnframes() * wav.getnchannels() * 2, path
        found += 1
    if pack.exists():
        assert {p.relative_to(pack).as_posix() for p in pack.rglob('*') if p.is_file()} <= {
            e['path'] for e in entries}, 'Unlisted music files'
    print('%d/%d optional title tracks verified.' % (found, len(entries)))


def check(root=ROOT, require_pack=False):
    rows = re.findall(r'SFX\((\w+),\s*"([a-z_/]+)",\s*(\d+),\s*(\d+),\s*(\d+)\)',
                      (root / 'src/ui_sound_map.h').read_text())
    defined = dict(re.findall(r'^#define SOUND_(\w+)\s+(\d+)',
                             (root / 'src/defines.h').read_text(), re.M))
    expected = {'SOUND_' + name for name in defined if name not in ('MAX', 'UNUSED')}
    assert len(rows) == len(expected) and {row[0] for row in rows} == expected, 'Sound event coverage'
    paths = set()
    for name, cue, count, gain, interval in rows:
        assert 1 <= int(count) <= 6 and 0 < int(gain) <= 100 and 0 <= int(interval) <= 1000, name
        paths.update('%s_%02d.wav' % (cue, n) for n in range(1, int(count) + 1))
    ambient_paths = set()
    for cue, count in re.findall(r'AMBIENT\(\w+,\s*"([a-z_/]+)",\s*(\d+)\)',
                                  (root / 'src/ui_ambient_map.h').read_text()):
        ambient_paths.update('%s_%02d.wav' % (cue, n) for n in range(1, int(count) + 1))
    assert len(ambient_paths) == 14, 'Ambient context coverage'
    paths.update(ambient_paths)
    entries = json.loads((root / 'lib/xtra/sound/pack-manifest.json').read_text())['files']
    assert len(entries) == len(paths) and {e['path'] for e in entries} == paths, 'Pack inventory differs from mapping'
    pack = root / 'lib/xtra/sound/pack'
    found = 0
    for entry in entries:
        path = pack / entry['path']
        if not path.is_file():
            assert not require_pack, 'Missing packaged sound: ' + entry['path']
            continue
        data = path.read_bytes()
        assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'], path
        with wave.open(str(path), 'rb') as wav:
            assert wav.getframerate() == 48000 and wav.getsampwidth() == 2, path
            assert wav.getnchannels() in (1, 2) and wav.getcomptype() == 'NONE', path
            assert 0 < wav.getnframes() <= 48000 * (65 if entry['path'] in ambient_paths else 3), path
            if entry['path'] in ambient_paths:
                assert wav.getnchannels() == 2 and wav.getnframes() >= 48000, path
            assert len(wav.readframes(wav.getnframes())) == wav.getnframes() * wav.getnchannels() * 2, path
        found += 1
    if pack.exists():
        assert {p.relative_to(pack).as_posix() for p in pack.rglob('*') if p.is_file()} <= paths, 'Unlisted audio files'
    print('%d sound events mapped; %d/%d optional effects/ambience verified.' % (len(rows), found, len(paths)))
    check_music(root, require_pack)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-pack', action='store_true')
    check(require_pack=parser.parse_args().require_pack)
