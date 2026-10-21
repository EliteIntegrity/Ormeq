"""SDL3 audio regressions. Requires SDL3 audio test targets and both game builds.

Fixtures are generated in a temporary directory, independent of licensed audio.
If the optional pack is installed, also decode every mapped variant.
"""
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
import sys
from test_music import run as run_music
from test_ambient import run as run_ambient
from test_audio_ui import run as run_audio_ui

ROOT = Path(__file__).resolve().parent.parent


def run(executable):
    with tempfile.TemporaryDirectory(prefix='ormeq-sound-') as folder:
        root = Path(folder)
        (root / 'combat').mkdir()
        for number, channels, rate in ((1, 2, 48000), (4, 1, 8000), (5, 1, 48000)):
            with wave.open(str(root / ('combat/hit_light_%02d.wav' % number)), 'wb') as wav:
                wav.setparams((channels, 2, rate, 480, 'NONE', 'not compressed'))
                frame = struct.pack('<hh', 8192, -4096) if channels == 2 else struct.pack('<h', 4096)
                wav.writeframes(frame * 480)
        (root / 'combat/hit_light_02.wav').write_bytes(b'damaged WAV')
        subprocess.run([str(executable), str(root)], check=True, cwd=ROOT)
    pack = ROOT / 'lib/xtra/sound/pack'
    if pack.exists():
        subprocess.run([str(executable), str(pack), '--full-pack'], check=True, cwd=ROOT)
    run_music(executable.with_name('ormeq-music-tests' + executable.suffix))
    run_ambient(executable.with_name('ormeq-ambient-tests' + executable.suffix))
    run_audio_ui()


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else
        ROOT / 'out-sdl3/Release/ormeq-sound-tests.exe')
