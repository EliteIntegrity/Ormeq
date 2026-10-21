"""Quest cards/journal in the real SDL renderer, using disposable saves only.

SDL's dummy video driver performs headless readbacks. --keep retains evidence.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import struct
import subprocess

from game_runtime import GameRuntime, ROOT

QUEST_PORTRAITS = [
    ('Uldrik II', 'uldrik-ii', 'Count', 1),
    ('Goodman Odwin', 'goodman-odwin', 'The White Horse Inn', 62),
    ('Denegor III', 'denegor-iii', 'Castle', 3),
    ('Bulin', 'bulin', 'Castle', 51),
    ('Hjolgar', 'hjolgar', "Fighters' Hall", 56),
    ('Paliro', 'paliro', "Thieves' Guild", 29),
    ('Colmanwen', 'colmanwen', 'Manor', 19),
    ('Zircon Jim', 'zircon-jim', "Mayor's Office", 22),
    ('Watts', 'watts', 'Police Station', 74),
    ('Sultan Idris', 'sultan-idris', 'Palace', 33),
    ('Lord Itsukama', 'lord-itsukama', 'Royal Lyceum', 63),
    ('Tanistil', 'tanistil', 'Tower of Sorcery', 77),
    ('Xeno', 'xeno', 'Chaos Tower', 11),
    ('Josh', 'josh', 'Nature Tower', 12),
    ('Lardbottom', 'lardbottom', "Thieves' Guild", 98),
    ('Kimuk', 'kimuk', 'Crossing Tower', 69),
]


FINISH_TOWNS = [
    ('anambar-mayor', (22, 67, 68), ["Mayor's Office:Zircon Jim:Human"] * 3),
    ('anambar-police', (74, 73, 72), ["Police Station:Watts:Human"] * 3),
    ('thalos-sultan', (33, 61, 71), ["Palace:Sultan Idris:Human"] * 3),
    ('thalos-wizards', (63, 76, 66), ["Royal Lyceum:Lord Itsukama:Human"] * 3),
    ('zul', (77, 10, 11), ["Tower of Sorcery:Tanistil:Elf", "Tower of Sorcery:Tanistil:Elf", "Chaos Tower:Xeno:Beastman"]),
    ('instant-stables', (98, 69, 35), ["Thieves' Guild:Lardbottom:Human", "Crossing Tower:Kimuk:Crossling", "Crossing Tower:Kimuk:Crossling"]),
]


def town_fixture(game, telmora=False, morivant=False, guilds=False, angwil=None, finish=None):
    rows = [list('.' * 198) for _ in range(66)]
    rows[30][85], rows[30][86], rows[30][83] = 'A', 'B', 'C'
    lines = ['T:TOWN:NORMAL:NO_ROTATE', 'L:.:FLOOR(GLOW | MARK)',
             'L:A:BUILDING_0(62)', 'L:B:BUILDING_1(1)', 'L:C:BUILDING_2(50)',
             'B:0:N:The White Horse Inn:Goodman Odwin:Human',
             'B:0:A:0:Request quest:0:0:q:6:0',
             'B:1:N:Count:Uldrik II:Human',
             'B:1:A:0:Request quest:0:0:q:6:0',
             'B:2:N:The White Horse Inn:Goodman Odwin:Human',
             'B:2:A:0:Request quest:0:0:q:6:0']
    lines += ['M:' + ''.join(row) for row in rows]
    if telmora or morivant:
        # Same disposable geometry, with real quest identities and contacts.
        for i, quest in enumerate((51, 55, 54) if morivant else (3, 5, 16)):
            lines[2 + i] = f'L:{"ABC"[i]}:BUILDING_{i}({quest})'
        owner = 'Bulin:Dwarf' if morivant else 'Denegor III:Human'
        lines = [re.sub(r'^(B:\d+:N:).*', r'\1Castle:' + owner, line)
                 for line in lines]
    if guilds:
        for i, quest in enumerate((56, 30, 32)):
            lines[2 + i] = f'L:{"ABC"[i]}:BUILDING_{i}({quest})'
        owners = ["Fighters' Hall:Hjolgar:Human", "Thieves' Guild:Paliro:Human", "Thieves' Guild:Paliro:Human"]
        lines = [re.sub(r'^(B:(\d+):N:).*', lambda m: m[1] + owners[int(m[2])], line)
                 for line in lines]
    if angwil is not None:
        for i, quest in enumerate((19, 35, 21)):
            lines[2 + i] = f'L:{"ABC"[i]}:BUILDING_{i}({quest})'
        owners = (["Manor:Colmanwen:Sidhe"] * 3 if angwil == 0 else
                  ["The White Horse Inn:Odwin:Human", "Crossing Tower:Kimuk:Crossling", "The White Horse Inn:Odwin:Human"])
        lines = [re.sub(r'^(B:(\d+):N:).*', lambda m: m[1] + owners[int(m[2])], line)
                 for line in lines]
    if finish is not None:
        _, ids, owners = FINISH_TOWNS[finish]
        for i, quest in enumerate(ids):
            lines[2 + i] = f'L:{"ABC"[i]}:BUILDING_{i}({quest})'
        lines = [re.sub(r'^(B:(\d+):N:).*', lambda m: m[1] + owners[int(m[2])], line)
                 for line in lines]
    content = '\n'.join(lines) + '\n'
    (game.root / 'lib/edit/t_outp.txt').write_text(content)
    if angwil in (1, 2):
        (game.root / 'lib/edit' / ('t_lite.txt' if angwil == 1 else 't_ulite.txt')).write_text(content)
    if finish == 5:
        (game.root / 'lib/edit/t_ulite.txt').write_text(content)


def run(keep=None):
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        town_fixture(game)
        fields, output, code = game.run(['-n', '-Q', '-uJOURNAL', '-s123', '-K2', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        save = game.root / 'lib/save/JOURNAL'
        original = save.read_bytes()
        character = 'JOURNAL'
        shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
        for dll in ROOT.glob('*.dll'):
            shutil.copy2(dll, game.root)
        shutil.copytree(ROOT / 'font', game.root / 'font')
        (game.root / 'lib/user/display.txt').write_text(
            'map_mode 0\nsquare yes\nfont plex\nfullscreen no\n'
            'weather off\ndaylight no\nsound no\nmusic no\n')
        grids = {}
        logs = {}

        def capture(name, width, height, script):
            save.write_bytes(original)
            shot = game.root / (name + '.png')
            result = subprocess.run([str(game.root / 'ormeq-sdl3.exe'), '-v', '-w', '-f16',
                f'-W{width}x{height}', '-z1.5', '-u' + character, '-P' + str(shot), '-x' + script],
                cwd=game.root, env=env, capture_output=True, text=True, timeout=35,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            assert result.returncode == 0, result.stdout + result.stderr
            logs[name] = result.stdout + result.stderr
            grid = re.findall(r'-> (\d+)x(\d+) cells at [\d.]+pt \(cell (\d+)x(\d+)\)', result.stdout)
            assert grid, result.stdout
            grids[width, height] = tuple(map(int, grid[-1]))
            assert shot.exists(), result.stdout + result.stderr
            assert struct.unpack('>II', shot.read_bytes()[16:24]) == (width, height)
            if keep:
                Path(keep).mkdir(parents=True, exist_ok=True)
                shutil.copy2(shot, Path(keep) / shot.name)
                (Path(keep) / (name + '.log')).write_text(result.stdout + result.stderr)
            fields, output, code = game.run(['-Q', '-u' + character, '-x~'])
            assert code == 0, output
            return {k: fields[k] for k in ('position', 'gold', 'experience', 'turns',
                    'hp', 'inventory', 'terrain', 'randomness')}

        baseline = capture('journal-control', 1280, 720, '~%p%xq')
        cases = [
            ('journal-wide', 1600, 900, '~%q%px%xq'),
            ('journal-active', 1280, 720, '~%q%px%xq'),
            ('journal-progress', 1280, 720, '~%qn%px%xq'),
            ('journal-completed', 1280, 720, '~%qc%px%xq'),
            ('journal-failed', 1280, 720, '~%qf%px%xq'),
            ('journal-small-list', 640, 480, '~%q%px%xq'),
            ('journal-small-detail', 640, 480, '~%q~%px%xq'),
            ('journal-small-scroll', 640, 480, '~%q~  %px%xq'),
            ('journal-help', 1280, 720, '~%q?%p^x%xq'),
            ('journal-reset', 1280, 720, '~%q%0x%xq'),
        ]
        for name, width, height, script in cases:
            assert capture(name, width, height, script) == baseline, name + ': browsing changed game/RNG'
        for width, height in [(1280, 720), (640, 480)]:
            cols, rows, cw, ch = grids[width, height]
            px, py = max(2, min(16, cols // 14)), max(2, min(8, rows // 10))
            def click(x, y):
                return '%%@%.2f,%.2f;' % ((width-cols*cw)/2 + x*cw, (height-rows*ch)/2 + y*ch)
            for label, action in [('tab', click(px+25, py+2.5)), ('row', click(px+6, py+7.5))]:
                assert capture(f'journal-click-{label}-{width}', width, height,
                               '~%q'+action+'%px%xq') == baseline
        visit = capture('quest-building-control', 1280, 720, '~;6%p^%xq')
        for name, width, height in [('quest-offer', 1280, 720), ('quest-offer-small', 640, 480)]:
            assert capture(name, width, height, '~;6q%p^^%xq') == visit, name + ': declining changed game/RNG'
        # Real optional portraits when installed; a tiny plate fixture in public
        # source builds exercises the same loader without publishing artwork.
        plates = {game.root / 'lib/art/shopkeepers' / (stem + '.art'): None
                  for stem in ('uldrik-ii', 'goodman-odwin')}
        for path in plates:
            plates[path] = path.read_bytes() if path.exists() else None
            path.parent.mkdir(parents=True, exist_ok=True)
            if plates[path] is None:
                path.write_bytes(b'FROGART2' + struct.pack('<4H', 8, 8, 80, 80)
                                 + bytes([255, 120, 160, 80]) * 64)
        try:
            for variant in ('present', 'missing', 'damaged'):
                for path in plates:
                    if variant == 'missing':
                        path.unlink()
                    elif variant == 'damaged':
                        path.write_bytes(b'invalid portrait fixture')
                for label, speaker, script, state in [
                        ('uldrik', 'Uldrik II', '~%q~%px%xq', baseline),
                        ('odwin', 'Goodman Odwin', '~;6q%p^^%xq', visit)]:
                    name = f'portrait-{label}-{variant}'
                    assert capture(name, 640, 480, script) == state, name + ': changed game/RNG'
                    expected = 'drawn' if variant == 'present' else 'no plate, drawing the letter'
                    assert f'portrait: collection 1, "{speaker}" -> {expected}' in logs[name], logs[name]
        finally:
            for path, content in plates.items():
                if content is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_bytes(content)
        capture('quest-receipt', 1280, 720, '~;6^;6q%p~^%xq')
        earned = save.read_bytes()
        capture('quest-failure', 1280, 720, '~;4q%p~^%xq')
        failed = save.read_bytes()
        capture('quest-accepted', 1280, 720, '~;6qa^%q%px%xq')
        (game.root / 'lib/user/display.txt').write_text(
            'density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        assert capture('journal-large-text', 640, 480, '~%q~%px%xq') == baseline
        # Reload the actual town turn-in and read its permanent receipt. This
        # also exercises larger text at 640x480, including the closing speech.
        original = earned
        earned_state = capture('receipt-loaded-control', 1280, 720, '~%p%xq')
        for name, width, height, script in [
                ('receipt-journal', 1280, 720, '~%qc%px%xq'),
                ('receipt-journal-small', 640, 480, '~%qc~%px%xq'),
                ('receipt-response-small', 640, 480, '~%qc~ %px%xq')]:
            assert capture(name, width, height, script) == earned_state, name

        original = failed
        failed_state = capture('failure-loaded-control', 1280, 720, '~%p%xq')
        for name, width, height, script in [
                ('failure-journal', 1280, 720, '~%qf%px%xq'),
                ('failure-journal-small', 640, 480, '~%qf~%px%xq'),
                ('failure-response-small', 640, 480, '~%qf~ %px%xq')]:
            assert capture(name, width, height, script) == failed_state, name
        _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
        assert code == 0 and 'quest-journal 2 ' in output, output
        assert 'quest-failure-response 50 1 ' in output, output
        # Remember selection both across closing/reopening and process restart.
        assert capture('journal-memory-reopen', 1280, 720, '~%qancx%q%px%xq') == failed_state
        _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
        assert code == 0 and 'quest-journal 1 ' in output, output
        remembered = re.search(r'^quest-journal (.+)$', output, re.M)[1]
        original = save.read_bytes()
        # Compare against the same save generation: saving itself advances
        # the existing RNG stream, independently of journal presentation.
        reload_control = capture('journal-memory-load-control', 640, 480, '~%p%xq')
        loaded_state = capture('journal-memory-loaded', 640, 480, '~%q%px%xq')
        assert loaded_state == reload_control, (loaded_state, reload_control)
        _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
        assert code == 0 and 'quest-journal ' + remembered in output, output

        town_fixture(game, telmora=True)
        fields, output, code = game.run(['-n', '-Q', '-uTELMORA', '-s123', '-K7', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        character = 'TELMORA'
        save = game.root / 'lib/save/TELMORA'
        original = save.read_bytes()
        (game.root / 'lib/user/display.txt').write_text(
            'font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        baseline = capture('telmora-control', 1280, 720, '~%p%xq')
        visit = capture('telmora-building-control', 1280, 720, '~;6%p^%xq')
        # The Windows frontend's minimum width is 640; also test a tall window.
        for width, height in ((1280, 720), (640, 480), (800, 480), (640, 800)):
            size = f'{width}x{height}'
            assert capture('telmora-offer-' + size, width, height, '~;6q%p^^%xq') == visit
            assert capture('telmora-ready-' + size, width, height, '~%q~%px%xq') == baseline
            assert capture('telmora-vault-' + size, width, height, '~%qn~%px%xq') == baseline
        capture('telmora-receipt', 1280, 720, '~;6^;6q%p~^%xq')
        capture('telmora-failure', 640, 480, '~;4q%p~^%xq')
        capture('telmora-accepted', 1280, 720, '~;6qa^%qn%px%xq')

        plate = game.root / 'lib/art/shopkeepers/denegor-iii.art'
        content = plate.read_bytes() if plate.exists() else None
        plate.parent.mkdir(parents=True, exist_ok=True)
        if content is None:
            plate.write_bytes(b'FROGART2' + struct.pack('<4H', 8, 8, 80, 80)
                              + bytes([255, 160, 120, 80]) * 64)
        try:
            for variant in ('present', 'missing', 'damaged'):
                if variant == 'missing': plate.unlink()
                elif variant == 'damaged': plate.write_bytes(b'invalid portrait fixture')
                name = 'portrait-denegor-' + variant
                assert capture(name, 640, 480, '~;6q%p^^%xq') == visit
                expected = 'drawn' if variant == 'present' else 'no plate, drawing the letter'
                assert f'portrait: collection 1, "Denegor III" -> {expected}' in logs[name], logs[name]
        finally:
            if content is None: plate.unlink(missing_ok=True)
            else: plate.write_bytes(content)
        (game.root / 'lib/user/display.txt').write_text(
            'density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        assert capture('telmora-large-offer', 640, 480, '~;6q%p^^%xq') == visit
        assert capture('telmora-large-vault', 640, 480, '~%qn~%px%xq') == baseline
        assert capture('telmora-large-scroll', 640, 480, '~%qn~ %px%xq') == baseline

        fields, output, code = game.run(['-n', '-Q', '-uFAILURE', '-s123', '-K8', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        character = 'FAILURE'
        save = game.root / 'lib/save/FAILURE'
        original = save.read_bytes()
        baseline = capture('failure-record-control', 1280, 720, '~%p%xq')
        for name, width, height, script in [
                ('failure-record-wide', 1280, 720, '~%q%px%xq'),
                ('failure-record-small', 640, 480, '~%q~%px%xq'),
                ('failure-record-scroll', 640, 480, '~%q~ %px%xq'),
                ('failure-record-unreported', 1280, 720, '~%qn%px%xq')]:
            assert capture(name, width, height, script) == baseline, name
        town_fixture(game, morivant=True)
        fields, output, code = game.run(['-n', '-Q', '-uMORIVANT', '-s123', '-K9', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        character = 'MORIVANT'
        save = game.root / 'lib/save/MORIVANT'
        original = save.read_bytes()
        (game.root / 'lib/user/display.txt').write_text(
            'font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        baseline = capture('morivant-control', 1280, 720, '~%p%xq')
        visit = capture('morivant-building-control', 1280, 720, '~;6%p^%xq')
        for width, height in ((1280, 720), (640, 480), (800, 480), (640, 800)):
            size = f'{width}x{height}'
            assert capture('morivant-offer-' + size, width, height, '~;6q%p^^%xq') == visit
            assert capture('morivant-ready-' + size, width, height, '~%q~%px%xq') == baseline
            assert capture('morivant-progress-' + size, width, height, '~%qn~%px%xq') == baseline
        capture('morivant-accepted', 1280, 720, '~;6qa^%qn%px%xq')
        capture('morivant-receipt', 1280, 720, '~;6^;6q%p~^%xq')
        earned = save.read_bytes()
        _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
        assert code == 0 and 'quest-receipt 55 ' in output, output
        assert 'Take my old battle gloves' in output and 'failures 0' in output, output
        capture('morivant-failure', 640, 480, '~;4q%p~^%xq')
        # Bulin needs no private artwork to use these cards. Missing and bad
        # plates both retain his glyph and exactly the same game/RNG state.
        plate = game.root / 'lib/art/shopkeepers/bulin.art'
        content = plate.read_bytes() if plate.exists() else None
        plate.parent.mkdir(parents=True, exist_ok=True)
        try:
            for variant in ('missing', 'damaged'):
                if variant == 'missing': plate.unlink(missing_ok=True)
                else: plate.write_bytes(b'invalid portrait fixture')
                name = 'portrait-bulin-' + variant
                assert capture(name, 640, 480, '~;6q%p^^%xq') == visit
                assert 'portrait: collection 1, "Bulin" -> no plate, drawing the letter' in logs[name], logs[name]
        finally:
            if content is None: plate.unlink(missing_ok=True)
            else: plate.write_bytes(content)
        (game.root / 'lib/user/display.txt').write_text(
            'density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        assert capture('morivant-large-offer', 640, 480, '~;6q%p^^%xq') == visit
        assert capture('morivant-large-scroll', 640, 480, '~;6q %p^^%xq') == visit
        original = earned
        earned_state = capture('morivant-receipt-control', 1280, 720, '~%p%xq')
        assert capture('morivant-receipt-loaded', 640, 480, '~%qc~ %px%xq') == earned_state

        town_fixture(game, guilds=True)
        _, output, code = game.run(['-n', '-Q', '-uGUILDS', '-s123', '-K10', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        character = 'GUILDS'
        save = game.root / 'lib/save/GUILDS'
        original = save.read_bytes()
        (game.root / 'lib/user/display.txt').write_text(
            'font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        baseline = capture('guilds-control', 1280, 720, '~%p%xq')
        visit = capture('guilds-building-control', 1280, 720, '~;6%p^%xq')
        for width, height in ((1280, 720), (640, 480), (800, 480), (640, 800)):
            size = f'{width}x{height}'
            assert capture('hjolgar-offer-' + size, width, height, '~;6q%p^^%xq') == visit
            assert capture('paliro-ready-' + size, width, height, '~%q~%px%xq') == baseline
            assert capture('paliro-progress-' + size, width, height, '~%qn~%px%xq') == baseline
            assert capture('hjolgar-progress-' + size, width, height, '~%qnn~%px%xq') == baseline
        capture('hjolgar-accepted', 1280, 720, '~;6qa^%qn%px%xq')
        capture('paliro-receipt', 1280, 720, '~;6^;6q%p~^%xq')
        earned = save.read_bytes()
        _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
        assert code == 0 and 'quest-receipt 30 ' in output and 'You kept your side' in output, output
        capture('paliro-failure', 640, 480, '~;4q%p~^%xq')
        failed = save.read_bytes()
        for speaker, script, state in [('Hjolgar', '~;6q%p^^%xq', visit), ('Paliro', '~%q~%px%xq', baseline)]:
            plate = game.root / 'lib/art/shopkeepers' / (speaker.lower() + '.art')
            content = plate.read_bytes() if plate.exists() else None
            plate.parent.mkdir(parents=True, exist_ok=True)
            try:
                for variant in ('missing', 'damaged'):
                    if variant == 'missing': plate.unlink(missing_ok=True)
                    else: plate.write_bytes(b'invalid portrait fixture')
                    name = f'portrait-{speaker.lower()}-{variant}'
                    assert capture(name, 640, 480, script) == state, name
                    assert f'portrait: collection 1, "{speaker}" -> no plate, drawing the letter' in logs[name], logs[name]
            finally:
                if content is None: plate.unlink(missing_ok=True)
                else: plate.write_bytes(content)
        (game.root / 'lib/user/display.txt').write_text(
            'density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
        assert capture('hjolgar-large-offer', 640, 480, '~;6q%p^^%xq') == visit
        assert capture('hjolgar-large-scroll', 640, 480, '~;6q %p^^%xq') == visit
        assert capture('paliro-large-progress', 640, 480, '~%qn~%px%xq') == baseline
        original = earned
        earned_state = capture('paliro-receipt-control', 1280, 720, '~%p%xq')
        assert capture('paliro-receipt-loaded', 640, 480, '~%qc~ %px%xq') == earned_state
        original = failed
        failed_state = capture('paliro-failure-control', 1280, 720, '~%p%xq')
        assert capture('paliro-failure-loaded', 640, 480, '~%qf~ %px%xq') == failed_state

        for mode in range(3):
            town_fixture(game, angwil=mode)
            character = f'ANGWIL{mode}'
            _, output, code = game.run(['-n', '-Q', '-u' + character, '-s123', f'-K{11+mode}', '-x~n~~^;2'])
            assert code == 0 and 'failures 0' in output, output
            save = game.root / 'lib/save' / character
            original = save.read_bytes()
            prefix = ('angwil', 'angwil-lite', 'angwil-instant')[mode]
            (game.root / 'lib/user/display.txt').write_text(
                'font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            baseline = capture(prefix + '-control', 1280, 720, '~%p%xq')
            visit = capture(prefix + '-building-control', 1280, 720, '~;6%p^%xq')
            sizes = ((1280, 720), (640, 480), (800, 480), (640, 800)) if mode == 0 else ((640, 480),)
            for width, height in sizes:
                size = f'{width}x{height}'
                assert capture(prefix + '-offer-' + size, width, height, '~;6q%p^^%xq') == visit
                assert capture(prefix + '-ready-' + size, width, height, '~%q~%px%xq') == baseline
                if mode < 2:
                    assert capture(prefix + '-quarry-' + size, width, height, '~%qn~%px%xq') == baseline
                    assert capture(prefix + '-later-' + size, width, height, '~%qnn~%px%xq') == baseline
            capture(prefix + '-accepted', 1280, 720, '~;6qa^%qn%px%xq')
            capture(prefix + '-receipt', 1280, 720, '~;6^;6q%p~^%xq')
            earned = save.read_bytes()
            _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
            assert code == 0 and 'quest-receipt 35 ' in output and 'additional reward we agreed upon' in output, output
            capture(prefix + '-failure', 640, 480, '~;4q%p~^%xq')
            failed = save.read_bytes()
            if mode == 0:
                plate = game.root / 'lib/art/shopkeepers/colmanwen.art'
                content = plate.read_bytes() if plate.exists() else None
                plate.parent.mkdir(parents=True, exist_ok=True)
                try:
                    for variant in ('missing', 'damaged'):
                        if variant == 'missing': plate.unlink(missing_ok=True)
                        else: plate.write_bytes(b'invalid portrait fixture')
                        name = 'portrait-colmanwen-' + variant
                        assert capture(name, 640, 480, '~;6q%p^^%xq') == visit
                        assert 'portrait: collection 1, "Colmanwen" -> no plate, drawing the letter' in logs[name], logs[name]
                finally:
                    if content is None: plate.unlink(missing_ok=True)
                    else: plate.write_bytes(content)
            (game.root / 'lib/user/display.txt').write_text(
                'density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            assert capture(prefix + '-large-offer', 640, 480, '~;6q%p^^%xq') == visit
            assert capture(prefix + '-large-scroll', 640, 480, '~;6q %p^^%xq') == visit
            if mode < 2:
                assert capture(prefix + '-quarry-scroll', 640, 480, '~%qn~ %px%xq') == baseline
            original = earned
            earned_state = capture(prefix + '-receipt-control', 1280, 720, '~%p%xq')
            assert capture(prefix + '-receipt-loaded', 640, 480, '~%qc~ %px%xq') == earned_state
            original = failed
            failed_state = capture(prefix + '-failure-control', 1280, 720, '~%p%xq')
            assert capture(prefix + '-failure-loaded', 640, 480, '~%qf~ %px%xq') == failed_state
        for group, (prefix, ids, _) in enumerate(FINISH_TOWNS):
            town_fixture(game, finish=group)
            character = f'FINISH{group}'
            _, output, code = game.run(['-n', '-Q', '-u' + character, '-s123', f'-K{20+group}', '-x~n~~^;2'])
            assert code == 0 and 'failures 0' in output, output
            save = game.root / 'lib/save' / character; original = save.read_bytes()
            (game.root / 'lib/user/display.txt').write_text('font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            baseline = capture(prefix + '-control', 1280, 720, '~%p%xq')
            visit = capture(prefix + '-visit', 1280, 720, '~;6%p^%xq')
            for width, height in ((1280, 720), (640, 480), (640, 800)):
                size = f'{width}x{height}'
                assert capture(prefix + '-offer-' + size, width, height, '~;6q%p^^%xq') == visit
                assert capture(prefix + '-ready-' + size, width, height, '~%q~%px%xq') == baseline
                assert capture(prefix + '-history-' + size, width, height, '~%qf~%px%xq') == baseline
            capture(prefix + '-receipt', 1280, 720, '~;6^;6q%p~^%xq')
            earned = save.read_bytes()
            _, output, code = game.run(['-Q', '-u' + character, '-K4', '-x~'])
            assert code == 0 and f'quest-receipt {ids[1]} ' in output and 'failures 0' in output, output
            capture(prefix + '-failure-reply', 640, 480, '~;4q%p~^%xq')
            (game.root / 'lib/user/display.txt').write_text('density Large\nfont plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            assert capture(prefix + '-large-offer', 640, 480, '~;6q%p^^%xq') == visit
            assert capture(prefix + '-large-scroll', 640, 480, '~;6q %p^^%xq') == visit
            original = earned
            earned_state = capture(prefix + '-earned-control', 1280, 720, '~%p%xq')
            assert capture(prefix + '-receipt-reload', 640, 480, '~%qc~ %px%xq') == earned_state

        for mode, prefix in ((15, 'dungeon-rewards'), (17, 'unseen-rewards'), (18, 'karrot-history'), (19, 'troika-history')):
            character = f'HISTORY{mode}'
            _, output, code = game.run(['-n', '-Q', '-u' + character, '-s123', f'-K{mode}', '-x~n~~^;2'])
            assert code == 0 and 'failures 0' in output, output
            save = game.root / 'lib/save' / character; original = save.read_bytes()
            (game.root / 'lib/user/display.txt').write_text('font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            baseline = capture(prefix + '-control', 1280, 720, '~%p%xq')
            for width, height in ((1280, 720), (640, 480)):
                assert capture(prefix + f'-{width}', width, height, '~%qc~7%px%xq') == baseline
                assert capture(prefix + f'-scroll-{width}', width, height, '~%qc~  %px%xq') == baseline
            if mode >= 18:
                assert capture(prefix + '-failed', 640, 480, '~%qf~%px%xq') == baseline
                assert capture(prefix + '-reopen', 640, 480, '~%qc~]]x%q~%px%xq') == baseline
    print('PASS: quest journal layouts, histories, portraits and glyph fallbacks; unchanged saved state/RNG')


def run_portraits(keep=None):
    """Every named contact through real offer cards, with and without artwork."""
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll, game.root)
        shutil.copytree(ROOT / 'font', game.root / 'font')
        for index, (name, stem, building, quest) in enumerate(QUEST_PORTRAITS):
            game.reset()
            town_fixture(game)
            town = game.root / 'lib/edit/t_outp.txt'
            text = town.read_text().replace('BUILDING_0(62)', f'BUILDING_0({quest})')
            text = text.replace('B:0:N:The White Horse Inn:Goodman Odwin:Human', f'B:0:N:{building}:{name}:Human')
            town.write_text(text)
            if index >= 14: (game.root / 'lib/edit/t_ulite.txt').write_text(text)
            _, output, code = game.run(['-n', '-Q', '-uPORTRAIT', '-s123', f'-K{100+index}', '-x~n~~^;2'])
            assert code == 0 and 'failures 0' in output, output
            save = game.root / 'lib/save/PORTRAIT'; original = save.read_bytes()
            (game.root / 'lib/user/display.txt').write_text('font plex\nfullscreen no\nsound no\nmusic no\nweather off\n')
            plate = game.root / 'lib/art/shopkeepers' / (stem + '.art')
            content = plate.read_bytes() if plate.exists() else None
            plate.parent.mkdir(parents=True, exist_ok=True)
            baseline = None
            try:
                for variant in ('installed', 'missing', 'damaged'):
                    if variant == 'missing': plate.unlink(missing_ok=True)
                    elif variant == 'damaged': plate.write_bytes(b'invalid portrait fixture')
                    sizes = ((1280, 720), (640, 480)) if variant == 'installed' else ((640, 480),)
                    for width, height in sizes:
                        label = f'portrait-{stem}-{variant}-{width}'
                        save.write_bytes(original)
                        shot = game.root / (label + '.png')
                        # One page scroll brings the contact band into the compact viewport.
                        script = '~;6q' + (' ' if width == 640 else '') + '%p^^%xq'
                        result = subprocess.run([str(game.root / 'ormeq-sdl3.exe'), '-v', '-w', '-f16',
                            f'-W{width}x{height}', '-uPORTRAIT', '-P' + str(shot), '-x' + script],
                            cwd=game.root, env=env, capture_output=True, text=True, timeout=35,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                        log = result.stdout + result.stderr
                        assert result.returncode == 0 and shot.exists(), log
                        if variant != 'installed' or content is None:
                            assert f'"{name}" -> no plate, drawing the letter' in log, log
                        else:
                            assert f'"{name}" -> ' in log and f'"{name}" -> no plate' not in log, log
                        fields, output, code = game.run(['-Q', '-uPORTRAIT', '-x~'])
                        assert code == 0, output
                        state = {k: fields[k] for k in ('position', 'gold', 'experience', 'turns', 'hp', 'inventory', 'terrain', 'randomness')}
                        if baseline is None: baseline = state
                        assert state == baseline, label + ': artwork changed gameplay/RNG'
                        if keep:
                            Path(keep).mkdir(parents=True, exist_ok=True)
                            shutil.copy2(shot, Path(keep) / shot.name)
                            (Path(keep) / (label + '.log')).write_text(log)
            finally:
                if content is None: plate.unlink(missing_ok=True)
                else: plate.write_bytes(content)
    print('PASS: all 16 questgiver portraits, compact cards and missing/damaged glyph fallbacks; unchanged state/RNG')


def run_clicks(keep=None):
    """First input after an actual idle pump, not a key that redraws the page.

    %! yields through the normal SDL event path without injecting input. This
    exercises the extra map-to-page repaint that ordinary scripted keys skip.
    Compare click results with keyboard results, not just unchanged gameplay.
    """
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    with GameRuntime() as game:
        town_fixture(game)
        _, output, code = game.run(['-n', '-Q', '-uCLICKS', '-s123', '-K2', '-x~n~~^;2'])
        assert code == 0 and 'failures 0' in output, output
        save = game.root / 'lib/save/CLICKS'
        original = save.read_bytes()
        shutil.copy2(ROOT / 'ormeq-sdl3.exe', game.root)
        for dll in ROOT.glob('*.dll'): shutil.copy2(dll, game.root)
        shutil.copytree(ROOT / 'font', game.root / 'font')

        def capture(label, width, height, keys):
            save.write_bytes(original)
            shot = game.root / (label + '.png')
            result = subprocess.run([str(game.root / 'ormeq-sdl3.exe'), '-v', '-w', '-f16',
                f'-W{width}x{height}', '-uCLICKS', '-P' + str(shot), '-x' + keys],
                cwd=game.root, env=env, capture_output=True, text=True, timeout=35,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            log = result.stdout + result.stderr
            assert result.returncode == 0 and shot.exists(), log
            pixels = shot.read_bytes()
            assert struct.unpack('>II', pixels[16:24]) == (width, height)
            grid = tuple(map(int, re.findall(
                r'-> (\d+)x(\d+) cells at [\d.]+pt \(cell (\d+)x(\d+)\)', log)[-1]))
            menu = tuple(map(int, re.findall(r'^capture-menu (\d+) (\d+) (-?\d+) (-?\d+)$', log, re.M)[-1]))
            if keep:
                Path(keep).mkdir(parents=True, exist_ok=True)
                shutil.copy2(shot, Path(keep) / shot.name)
                (Path(keep) / (label + '.log')).write_text(log)
            fields, output, code = game.run(['-Q', '-uCLICKS', '-K4', '-x~'])
            assert code == 0 and 'failures 0' in output, output
            state = {k: fields[k] for k in ('position', 'gold', 'experience', 'turns',
                     'hp', 'inventory', 'terrain', 'randomness')}
            journal = re.search(r'^quest-journal (.+)$', output, re.M)[1]
            return pixels, grid, menu, state, journal

        for mode in (0, 1, 2):
            for width, height in ((1280, 720), (640, 480)):
                label = f'first-click-{mode}-{width}'
                (game.root / 'lib/user/display.txt').write_text(
                    f'map_mode {mode}\ntile_detail 64\nsquare yes\nfont plex\nfullscreen no\n'
                    'weather off\ndaylight no\nsound no\nmusic no\n')
                control = capture(label + '-map', width, height, '~%!%p%xq')
                first = capture(label + '-drawn', width, height, '~%!%q%px%xq')
                idle = capture(label + '-idle', width, height, '~%!%q%!%px%xq')
                assert first[2][0] == idle[2][0] == 0
                assert first[2][1] > 0 and first[2][2] >= 0
                assert first[2] == idle[2], label + ': idle repaint discarded menu rows/selection'
                assert first[0] == idle[0], label + ': initial highlight changed before input'
                assert first[3] == idle[3] == control[3], label + ': opening changed gameplay/RNG'
                cols, rows, cw, ch = first[1]
                px, py = max(2, min(16, cols // 14)), max(2, min(8, rows // 10))
                page_width, page_height = cols - 2 * px, rows - 2 * py
                footer_y = py + page_height - 2
                wide = page_width >= 92 and page_height >= 20 and width >= 960
                tab_width = page_width // 3 if page_width < 60 else 20

                def click(x, y):
                    # Replay coordinates are grid-relative; the SDL handler
                    # adds the window's centred-grid origin exactly once.
                    return f'%@{x * cw:.2f},{y * ch:.2f};'

                cases = [
                    ('completed', 'c', click(px + tab_width + 3, py + 2.5), 'x%xq'),
                    ('failed', 'f', click(px + 2 * tab_width + 3, py + 2.5), 'x%xq'),
                    ('next', 'n', click(px + (15 if page_width >= 64 else 6), footer_y + .5), 'x%xq'),
                    ('row', 'n' + ('' if wide else '~'), click(px + 4, py + 7.5), 'x%xq'),
                    ('help', '?', click(px + (42 if page_width >= 64 else 19), footer_y + .5), '^x%xq'),
                    ('close', 'x', click(px + (53 if page_width >= 64 else 24), footer_y + .5), '%xq'),
                ]
                for name, key, mouse, suffix in cases:
                    reference = capture(label + '-' + name + '-key', width, height,
                                        '~%!%q%!' + key + '%!%p' + suffix)
                    clicked = capture(label + '-' + name + '-mouse', width, height,
                                      '~%!%q%!' + mouse + '%!%p' + suffix)
                    assert reference[0] == clicked[0], (label, name, 'first click differs from keyboard')
                    assert reference[4] == clicked[4], (label, name, 'journal selection differs')
                    assert reference[3] == clicked[3] == control[3], (label, name, 'gameplay/RNG changed')
                    if name == 'completed': assert clicked[4].startswith('1 ')
                    if name == 'failed': assert clicked[4].startswith('2 ')
                    if name == 'close':
                        assert clicked[2] == (1, 0, -1, -1), 'Stale page rows survived return to map'
    print('PASS: first-frame highlight and first clicks on tabs, rows, navigation, Help and Close; '
          'ASCII/Hybrid/Full Tile at 1280/640, stale rows cleared, unchanged gameplay/RNG')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keep', type=Path)
    parser.add_argument('--portraits-only', action='store_true')
    parser.add_argument('--clicks-only', action='store_true')
    args = parser.parse_args()
    if args.clicks_only: run_clicks(args.keep)
    elif args.portraits_only: run_portraits(args.keep)
    else:
        run(args.keep)
        run_portraits(args.keep)
        run_clicks(args.keep)
