"""First-input regressions for SDL menus, with isolated saves and dummy drivers."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess

from game_runtime import GameRuntime, ROOT
from test_quest_ui import town_fixture

# name, route, first action, route back after that action, route back unchanged.
CASES = [
    ('pause', '~%!^', 's', 'xc%xq', 'c%xq'),
    ('settings', '~%!^%!s', 'u', 'xxc%xq', 'xc%xq'),
    ('sound', '~%!^%!s%!u', 'f', 'xxc%xq', 'xxc%xq'),
    ('ambience', '~%!^%!s%!u%!a', 'a', 'xxxc%xq', 'xxxc%xq'),
    ('music', '~%!^%!s%!u%!m', 'm', 'xxxc%xq', 'xxxc%xq'),
    ('effects', '~%!^%!e', '\x1b', 'c%xq', '^c%xq'),
    ('messages', '~%!%P', '\t', '^%xq', '^%xq'),
    ('past-reckonings', '~%!%P%!%i', '\t', '^%xq', '^%xq'),
    ('reckoner-help', '~%!%P%!%i%!?', '\x1b', '^%xq', '^^%xq'),
    ('help', '~%!?', '\x1b', '%xq', '^%xq'),
    ('options', '~%!=', '2', '^^%xq', '^%xq'),
    ('knowledge', '~%!\\~', 'd', '^^%xq', '^%xq'),
    ('inventory', '~%!i', 'a', '^^%xq', '^%xq'),
    ('equipment', '~%!e', 'a', '^^%xq', '^%xq'),
    ('inspect', '~%!I', 'a', '^^%xq', '^%xq'),
    ('wield', '~%!ta%!w', 'b', 'a%xq', '^%xq'),
    ('wield-inspect', '~%!ta%!wA', None, '^^%xq', '^^%xq'),
    ('spellbook', '~%!Gaa%!m', 'a', '^%xq', '^%xq'),
    ('spell-choice', '~%!Gaa%!ma', None, '^%xq', '^%xq'),
    ('spell-browse', '~%!ba', None, '^%xq', '^%xq'),
    ('power-choice', '~%!U', None, '^%xq', '^%xq'),
    ('character', '~%!C', None, '^%xq', '^%xq'),
    ('character-quests', '~%!C6666', 'j', 'x^%xq', '^%xq'),
    ('pause-return', '~%!^s%!x', 'c', '%xq', 'c%xq'),
    ('settings-return', '~%!^s%!u%!x', 'u', 'xxc%xq', 'xc%xq'),
    ('knowledge-return', '~%!\\~d%!^', 'q', 'x^%xq', '^%xq'),
    ('map-options', '~%!=2', None, '^^%xq', '^^%xq'),
    ('loot-judgement', '~%!=p', None, '^^%xq', '^^%xq'),
    ('building', '~%!;6', 'q', '^^%xq', '^%xq'),
    ('quest-offer', '~%!;6q', '\x1b', '^%xq', '^^%xq'),
    ('shop', '~%!;8', None, '^%xq', '^%xq'),
    ('shop-return', '~%!;8?%!^', None, '^%xq', '^%xq'),
    ('shop-help', '~%!;8?', '\x1b', '^%xq', '^^%xq'),
    ('home', '~%!;2', None, '^%xq', '^%xq'),
    ('home-return', '~%!;2?%!^', None, '^%xq', '^%xq'),
    ('home-help', '~%!;2?', '\x1b', '^%xq', '^^%xq'),
    ('map-return-pause', '~%!^s%!xc', None, '%xq', '%xq'),
    ('map-return-help', '~%!?%!^', None, '%xq', '%xq'),
    ('map-return-shop', '~%!;8?%!^^', None, '%xq', '%xq'),
    ('title', '', 's', 'xq', 'q'),
    ('title-settings', 's', 'u', 'xxq', 'xq'),
    ('title-sound', 's%!u', 'a', 'xxxq', 'xxq'),
    ('title-world', 'w', '\x1b', 'q', '^q'),
    ('title-credits', 't', '\x1b', 'q', '^q'),
    ('title-load', 'l', 'a', '%xq', '^q'),
    ('title-graveyard', 'd', None, '^q', '^q'),
    ('birth', 'n', '?', '^^q', '^q'),
    ('birth-identity', 'nn', 's', '^^q', '^^q'),
    ('birth-race-groups', 'nnr', 'a', '^^^^q', '^^^q'),
    ('birth-races', 'nnra', 'a', '^^q', '^^^^q'),
    ('birth-class-groups', 'nnc', 'a', '^^^^q', '^^^q'),
    ('birth-classes', 'nnca', 'a', '^^q', '^^^^q'),
]


def script_key(key):
    return '^' if key == '\x1b' else '~' if key in ('\n', '\r') else (
        '%i' if key == '\t' else '\\' + key if key in '~[]{}\\' else key)


class MenuRun:
    def __init__(self, game, keep):
        self.game = game
        self.keep = Path(keep) if keep else None
        self.files = {p.relative_to(game.root): p.read_bytes()
                      for folder in ('save', 'apex', 'bone', 'user', 'data')
                      for p in (game.root / 'lib' / folder).rglob('*') if p.is_file()}
        self.env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                        SDL_RENDER_DRIVER='software')

    def capture(self, label, keys, width=1280, height=720, mode=0, probe=False,
                calculations='animated'):
        for folder in ('save', 'apex', 'bone', 'user', 'data'):
            for path in (self.game.root / 'lib' / folder).rglob('*'):
                if path.is_file(): path.unlink()
        for path, content in self.files.items():
            target = self.game.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        (self.game.root / 'lib/user/display.txt').write_text(
            f'map_mode {mode}\ntile_detail 64\nsquare yes\nfont plex\nfullscreen no\n'
            'weather off\ndaylight no\nsound no\nmusic no\n'
            f'calculations {calculations}\n')
        shot = self.game.root / (label + '.png')
        args = [str(self.game.root / 'ormeq-sdl3.exe'), '-v', '-w', '-f16',
                f'-W{width}x{height}', '-uMENUS', '-P' + str(shot), '-x%!%!' + keys]
        try:
            result = subprocess.run(args, cwd=self.game.root, env=self.env,
                capture_output=True, text=True, errors='replace', timeout=12 if probe else 35,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            code, log = result.returncode, result.stdout + result.stderr
        except subprocess.TimeoutExpired as exc:
            code = -1
            log = ''.join(s.decode(errors='replace') if isinstance(s, bytes) else s
                          for s in (exc.stdout or '', exc.stderr or ''))
        if self.keep:
            self.keep.mkdir(parents=True, exist_ok=True)
            (self.keep / (label + '.log')).write_text(log)
            if shot.exists(): shutil.copy2(shot, self.keep / shot.name)
        assert shot.exists(), (label, code, log[-1000:])
        menu = tuple(map(int, re.findall(r'^capture-menu (\d+) (\d+) (-?\d+) (-?\d+)$', log, re.M)[-1]))
        home, cells = map(int, re.findall(r'^capture-cells (\d+) (\d+)$', log, re.M)[-1])
        panel = tuple(map(int, re.findall(r'^capture-panel (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$', log, re.M)[-1]))
        reckoner = re.findall(r'^capture-reckoner (.+)$', log, re.M)[-1].split()
        rows = [tuple(map(int, row)) for row in re.findall(r'^capture-row (\d+) (\d+) (\d+) (\d+)$', log, re.M)]
        grid = tuple(map(int, re.findall(
            r'-> (\d+)x(\d+) cells at [\d.]+pt \(cell (\d+)x(\d+)\)', log)[-1]))
        if not probe: assert code == 0, (label, log[-1000:])
        fields, output, rc = self.game.run(['-Q', '-uMENUS', '-x~'])
        assert rc == 0, output
        state = {k: fields[k] for k in ('position', 'gold', 'experience', 'turns', 'hp',
                 'inventory', 'terrain', 'randomness')}
        return dict(cells=cells, panel=panel, reckoner=reckoner, visual=(home, cells if home else shot.read_bytes()),
                    menu=menu, rows=rows, grid=grid, state=state, code=code)


def check_reckoner(runner, mode, width, height):
    label = f'reckoner-{mode}-{width}'

    def capture(suffix, keys, setting='animated'):
        return runner.capture(label + suffix, keys, width, height, mode,
                              calculations=setting)

    ready = capture('-ready', '~%!%p%xq')
    aiming = capture('-direction', '~%!;%!%p^%xq')
    assert ready['reckoner'][:2] == ['1', '1'], 'Details unavailable at command prompt'
    assert aiming['reckoner'][:2] == ['1', '0'], 'Direction prompt did not guard Details'
    assert ready['reckoner'][2:] == aiming['reckoner'][2:], 'Details pixels changed between input states'
    assert ready['state'] == aiming['state'], 'Cancelled direction changed gameplay/RNG'
    x, y, w, h = map(float, ready['reckoner'][3:])
    point = f'{x + w / 2:.3f},{y + h / 2:.3f};'
    blocked = capture('-guarded-click', '~%!;%!%@' + point + '%!%p^%xq')
    assert blocked['visual'] == aiming['visual'], 'Details interrupted a direction prompt'
    assert blocked['state'] == aiming['state'], 'Guarded click changed gameplay/RNG'
    keyboard = capture('-history-key', '~%!%P%i%!%p^%xq')
    clicked = capture('-history-click', '~%!%@' + point + '%!%p^%xq')
    assert clicked['visual'] == keyboard['visual'], 'Details did not open Past Reckonings'
    assert clicked['state'] == ready['state'], 'Opening Details changed gameplay/RNG'
    # Same actual movement, with the observer off/static/animated.
    moved = [capture('-move-' + setting, '~%!;7%!%p%xq', setting)
             for setting in ('off', 'static', 'animated')]
    assert moved[0]['reckoner'] == ['0'], 'Off setting left live panel visible'
    assert moved[1]['reckoner'][0] == moved[2]['reckoner'][0] == '1'
    assert moved[0]['state'] == moved[1]['state'] == moved[2]['state'], 'Display setting changed movement/RNG'
    print('PASS:', label, 'steady Details pixels, input guard, history and optional display', flush=True)


def check_specialists(keep, configurations):
    # Create these through normal character creation, not edited saves. These
    # classes have separate legacy chooser implementations, not book casting.
    for name, birth, setup, cls in (
        ('gray-mage', 'cdca', '', 46),
        ('magic-eater', 'cec', '', 16),
        ('samurai', 'cafa', 'Gaa', 19),
        ('alchemist', 'cea', '', 50),
        ('mindcrafter', 'cja', '', 9),
    ):
        with GameRuntime() as game:
            town_fixture(game)
            fields, output, code = game.run(['-n', '-Q', '-uMENUS', '-s123',
                '-x~n' + birth + '~~^;2%s'])
            assert code == 0 and fields.get('who') == f'race 3 class {cls}', output
            shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
            for dll in ROOT.glob('*.dll'): shutil.copy2(dll, game.root)
            shutil.copytree(ROOT / 'font', game.root / 'font')
            runner = MenuRun(game, keep)
            for mode, width, height in configurations:
                label = f'{name}-{mode}-{width}'
                def capture(suffix, keys):
                    return runner.capture(label + suffix, '~%!' + setup + keys,
                                          width, height, mode)
                baseline = capture('-map', '%!%p%xq')
                drawn = capture('-drawn', 'm%p^%xq')
                idle = capture('-idle', 'm%!%!%p^%xq')
                cols, rows, _, _ = idle['grid']
                assert idle['panel'] == (1, 0, 0, cols, rows, 0), label
                assert idle['menu'][0] == 0 and not idle['rows'], label
                # Legacy keyboard-only headings acquire the theme's heading
                # colour on their first repaint, as in the shop routes above.
                assert drawn['cells'] == idle['cells'], label
                assert drawn['panel'] == idle['panel'] and drawn['menu'] == idle['menu'], label
                assert drawn['state'] == idle['state'] == baseline['state'], label
                returned = capture('-return', 'm^%!%p%xq')
                assert returned['menu'][0] == 1 and not returned['rows'], label
                assert returned['state'] == baseline['state'], label
                print('PASS:', label, 'clean chooser, first wait, map return and unchanged state/RNG', flush=True)


def run(keep=None, probe=False, names=None, configurations=None):
    failures = []
    configurations = configurations or [(0, 1280, 720), (2, 640, 480), (1, 1280, 720)]
    with GameRuntime() as game:
        town_fixture(game)
        town = game.root / 'lib/edit/t_outp.txt'
        content = town.read_text().replace('L:.:FLOOR(GLOW | MARK)',
            'L:.:FLOOR(GLOW | MARK)\nL:S:GENERAL_STORE\nL:H:HOME')
        lines = content.splitlines()
        map_rows = [i for i, line in enumerate(lines) if line.startswith('M:')]
        # The wilderness town template starts one 66x22 sector into the map.
        for y, symbol in ((7, 'S'), (9, 'H')):
            line = list(lines[map_rows[y]])
            line[2 + 18] = symbol
            lines[map_rows[y]] = ''.join(line)
        town.write_text('\n'.join(lines) + '\n')
        for name in ('t_lite.txt', 't_ulite.txt'):
            (game.root / 'lib/edit' / name).write_text(town.read_text())
        _, output, code = game.run(['-n', '-Q', '-uMENUS', '-s123', '-K2', '-x~n~~^;2%s'])
        assert code == 0, output
        shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll, game.root)
        shutil.copytree(ROOT / 'font', game.root / 'font')
        runner = MenuRun(game, keep)
        for mode, width, height in configurations:
            if not probe and (not names or 'reckoner' in names):
                try:
                    check_reckoner(runner, mode, width, height)
                except (AssertionError, IndexError) as exc:
                    failures.append((f'reckoner-{mode}-{width}', str(exc)))
                    print('FAIL: reckoner', mode, width, str(exc), flush=True)
            for name, route, key, finish, cancel in CASES:
                if names and name not in names: continue
                label = f'{name}-{mode}-{width}'
                def capture(suffix, keys):
                    return runner.capture(label + suffix, keys, width, height, mode, probe)
                try:
                    if probe:
                        result = capture('', route + '%!%p' + cancel)
                        print(label, result['code'], result['menu'],
                              ''.join(chr(row[0]) if row[0] >= 32 else f'<{row[0]}>' for row in result['rows']), flush=True)
                        continue
                    first = capture('-drawn', route + '%p' + cancel)
                    idle = capture('-idle', route + '%!%!%p' + cancel)
                    on_map = name.startswith('map-return-')
                    # Returning to the map intentionally retires the menu's
                    # rows during the first event pass. A menu must keep them.
                    if not on_map:
                        assert first['menu'] == idle['menu'] and first['rows'] == idle['rows'], 'Rows/selection lost on first wait'
                    assert first['state'] == idle['state'], 'Idle changed gameplay/RNG'
                    assert first['cells'] == idle['cells'], 'First wait changed the text'
                    # Legacy keyboard-only shops restyle their heading on the
                    # first repaint. They have no pointer controls to lose.
                    if key is not None:
                        assert first['visual'] == idle['visual'], 'First wait changed the display'
                    assert idle['menu'][0] == int(on_map), 'Wrong map visibility'
                    if name in ('pause', 'pause-return'):
                        assert {row[0] for row in idle['rows']} == set(map(ord, 'cseqa')), 'Unexpected pause entries'
                    if name.startswith(('wield', 'spell', 'power-choice')):
                        cols, rows, _, _ = idle['grid']
                        assert idle['panel'] == (1, 0, 0, cols, rows, 0), 'Chooser left map/HUD cells behind'
                    if key is None:
                        assert not idle['rows'], 'Keyboard-only page retained stale mouse rows'
                        print('PASS:', label, 'first wait, no stale mouse rows', flush=True)
                        continue
                    matching = [(i, row) for i, row in enumerate(idle['rows']) if row[0] == ord(key)]
                    assert matching, 'Expected first action is not clickable: ' + repr(key)
                    index, (_, x, y, w) = matching[0]
                    cols, rows, cw, ch = idle['grid']
                    assert 0 <= x < cols and 0 <= y < rows and w > 0, 'Action lies outside the screen'
                    point = f'{(x + min(w / 2, 4)) * cw:.2f},{(y + .5) * ch:.2f};'
                    hover = capture('-hover', route + '%!%&' + point + '%p' + cancel)
                    assert hover['menu'][3] == index, 'First hover missed its row'
                    assert hover['rows'] == idle['rows'], 'Hover lost menu rows'
                    assert hover['state'] == idle['state'], 'Hover changed gameplay/RNG'
                    reference = capture('-key', route + '%!' + script_key(key) + '%!%p' + finish)
                    clicked = capture('-mouse', route + '%!%@' + point + '%!%p' + finish)
                    assert reference['menu'] == clicked['menu'] and reference['rows'] == clicked['rows'], 'First click opened a different screen'
                    assert reference['visual'] == clicked['visual'], 'First click differs from keyboard result'
                    assert reference['state'] == clicked['state'], 'First click changed gameplay/RNG'
                    assert reference['visual'] != idle['visual'], 'Action did not change the screen'
                    print('PASS:', label, 'first wait, hover and click', flush=True)
                except (AssertionError, IndexError) as exc:
                    failures.append((label, str(exc)))
                    print('FAIL:', label, str(exc), flush=True)
    assert not failures, failures
    if not probe and (not names or 'specialist-pages' in names):
        check_specialists(keep, configurations)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep', type=Path)
    parser.add_argument('--probe', action='store_true')
    parser.add_argument('--case', action='append', choices=['reckoner', 'specialist-pages'] + [case[0] for case in CASES])
    parser.add_argument('--mode', type=int, choices=(0, 1, 2))
    parser.add_argument('--small', action='store_true')
    args = parser.parse_args()
    configs = [(args.mode, 640, 480) if args.small else (args.mode, 1280, 720)] if args.mode is not None else None
    run(args.keep, args.probe, args.case, configs)
