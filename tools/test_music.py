"""Title-music tests use generated fixtures, never a private music master."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parent.parent


def run(executable):
    with tempfile.TemporaryDirectory(prefix='ormeq-music-') as tmp:
        root = Path(tmp)
        for name, channels, rate, width in (('stereo', 2, 48000, 2),
                                           ('mono', 1, 44100, 2),
                                           ('rate', 1, 8000, 2),
                                           ('wide', 1, 48000, 3)):
            with wave.open(str(root / (name + '.wav')), 'wb') as wav:
                wav.setparams((channels, width, rate, rate, 'NONE', 'not compressed'))
                frame = (struct.pack('<hh', 8192, -4096) if channels == 2 else
                         struct.pack('<h', 8192) if width == 2 else b'\x00\x00\x20')
                wav.writeframes(frame * rate)
        (root / 'corrupt.wav').write_bytes(b'broken file')
        (root / 'empty.wav').write_bytes(b'')
        (root / 'truncated.wav').write_bytes((root / 'stereo.wav').read_bytes()[:-123])
        with (root / 'large.wav').open('wb') as f:
            f.truncate(128 * 1024 * 1024 + 1)
        subprocess.run([str(executable), str(root)], check=True, cwd=ROOT)
    track = ROOT / 'lib/xtra/music/pack/title.wav'
    if track.is_file():
        subprocess.run([str(executable), '--track', str(track)], check=True, cwd=ROOT)


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else
        ROOT / 'out-sdl3/Release/ormeq-music-tests.exe')
