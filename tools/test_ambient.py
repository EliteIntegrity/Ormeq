"""Ambience regression tests with generated PCM and no private audio dependency."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parent.parent


def run(executable):
    with tempfile.TemporaryDirectory(prefix='ormeq-ambient-') as tmp:
        root = Path(tmp)
        ambient = root / 'ambient'
        ambient.mkdir()
        for name, rate in [('surface_day_01', 48000), ('surface_night_01', 8000),
                           ('meridian_middle_01', 48000)]:
            with wave.open(str(ambient / (name + '.wav')), 'wb') as wav:
                wav.setparams((2, 2, rate, rate, 'NONE', 'not compressed'))
                wav.writeframes(struct.pack('<hh', 8192, -4096) * rate)
        (ambient / 'surface_day_02.wav').write_bytes(b'bad WAV')
        path = ambient / 'meridian_middle_01.wav'
        path.write_bytes(path.read_bytes()[:-123])
        with (ambient / 'meridian_deep_01.wav').open('wb') as f:
            f.truncate(16 * 1024 * 1024 + 1)
        subprocess.run([str(executable), str(root)], check=True, cwd=ROOT)
    pack = ROOT / 'lib/xtra/sound/pack'
    if (pack / 'ambient').is_dir():
        subprocess.run([str(executable), str(pack), '--full-pack'], check=True, cwd=ROOT)


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else
        ROOT / 'out-sdl3/Release/ormeq-ambient-tests.exe')
