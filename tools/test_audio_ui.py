"""Exercise the actual SDL audio settings with dummy video/audio, no window.

Uses a private installation and never reads or changes the player's saves.
"""
import os
from pathlib import Path
import shutil
import subprocess

from game_runtime import GameRuntime, ROOT


def run():
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
        for path in ROOT.glob('*.dll'):
            shutil.copy2(path, game.root)
        shutil.copytree(ROOT / 'font', game.root / 'font')
        for keys, on, volume in [('sufnaa-xmmxxxq', 'no', 20),
                                 ('sufnaa+xmmxxxq', 'yes', 25)]:
            p = subprocess.run([str(game.root / 'ormeq-sdl3.exe'), '-w', '-f14', '-x'+keys],
                               cwd=game.root, env=env, capture_output=True, text=True,
                               timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            assert p.returncode == 0, p.stdout + p.stderr
            prefs = dict(line.split() for line in (game.root/'lib/user/display.txt').read_text().splitlines()
                         if line and not line.startswith('#'))
            for key in ('footsteps', 'menu_sounds', 'ambient', 'music'):
                assert prefs[key] == on, (key, prefs)
            assert prefs['ambient_volume'] == str(volume), prefs
            assert prefs['sound'] == 'yes' and prefs['sound_volume'] == '70', prefs
            assert not any((game.root/'lib/save').iterdir()), 'Title settings created a game save'
    print('PASS: SDL settings navigation and persistent independent audio controls, dummy video/audio')


if __name__ == '__main__':
    run()
