"""Run the real SDL sprite loader against synthetic media, without a window."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run():
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT/'out-sdl3/Release/ormeq-tile-tests.exe'
    with tempfile.TemporaryDirectory(prefix='ormeq-tiles-') as directory:
        args = [str(exe.resolve()), directory]
        pack = ROOT/'lib/tiles/pack'
        if (pack/'pack-64.txt').is_file():
            args.append(str(pack))
        subprocess.run(args, check=True, timeout=60,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


if __name__ == '__main__':
    run()
