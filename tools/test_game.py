"""Run a small gameplay regression suite in private temporary installations.

    python tools/test_game.py
    powershell -File build.ps1 -Test

Requires a current headless build. No live saves or preferences are copied.
"""
from pathlib import Path
import os
import re
import struct
import tempfile
import unittest

from game_runtime import GameRuntime, MUTABLE
from check_identity_rules import catalogue, check, conditions
from soak import WALK, Sweep, seed_values

STATE = ('position', 'gold', 'experience', 'turns', 'level', 'hp', 'depth',
         'who', 'stats', 'world', 'inventory', 'terrain')
BIRTH = '~n~~^'  # dismiss the birth messages before sending game commands
WALK_TOWN = ';2;2;6;6'


def decode_save(data):
    return bytes(a ^ b for a, b in zip(data[5:], data[4:-1]))[:-8]


def saved_option_flags(path):
    """Read actual persisted options independently of the menu's display.

    The current save header is 12 bytes, followed by a 16-bit RNG position,
    63 RNG words, then 15 bytes of scalar/cheat/autosave options.
    """
    body = decode_save(path.read_bytes())
    return struct.unpack_from('<8I', body, 12 + 2 + 63 * 4 + 15)


def encode_save(original, body, version=None):
    """Independent encoding of the XOR stream and both save checksums."""
    data = bytearray(version or original[:4]) + original[4:5]
    previous = data[-1]
    for value in body + struct.pack('<I', sum(body) & 0xffffffff):
        previous ^= value
        data.append(previous)
    checksum = sum(data[5:]) & 0xffffffff
    for value in struct.pack('<I', checksum):
        previous ^= value
        data.append(previous)
    return bytes(data)


def check_reckoner_arithmetic(output):
    """Independent arithmetic oracle, including C division towards zero."""
    def divide(a, b):
        assert b > 0, 'invalid divisor'
        return (abs(a) // b) * (-1 if a < 0 else 1)
    count = 0
    for line in output.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'effect-arith':
            before, after, mul, div, bias, add, subtract = map(int, parts[1:])
            expected = before - divide(before * mul, div) if subtract else divide(before * mul + bias, div) + add
            assert after == expected, line
        elif parts[0] == 'effect-check':
            left, op, right, result = parts[1:]
            left, right, result = int(left), int(right), int(result)
            expected = {'>': left > right, '>=': left >= right,
                        '<': left < right, '<=': left <= right,
                        '==': left == right, '!=': left != right}[op]
            assert result == expected, line
        elif parts[0] == 'effect-roll':
            value, low, high = map(int, parts[1:])
            assert low <= value <= high, line
        elif parts[0] == 'effect-dice':
            dd, ds, total, *faces = map(int, parts[1:])
            assert len(faces) == dd and sum(faces) == total, line
            assert all(1 <= face <= ds for face in faces), line
        else:
            continue
        count += 1
    return count


class RuntimeTests(unittest.TestCase):
    def test_birth_return_sounds_only_when_accepted(self):
        with GameRuntime() as game:
            def accepts(route):
                game.reset()
                _, output, code = game.run(['-n', '-Q', '-uBIRTH', '-s123',
                                            '-A1', '-x' + route])
                self.assertEqual(code, 0, output)
                found = re.search(r'^audio-event ui_accept (\d+)$', output, re.M)
                return int(found[1]) if found else 0

            # None of these pages acts on Return (including over-budget stats).
            for route in ('~', '~np', '~ng', '~nr', '~nc', '~nrac', '~ncde', '~npt',
                          '~n~S'):
                with self.subTest(ignored=route):
                    self.assertEqual(accepts(route + '~'), accepts(route))
            # Summary, current race/class and patron all act on Return.
            for route in ('~n', '~nrc', '~ncg', '~nchb', '~nchaa~', '~npta', '~nptab', '~n~'):
                with self.subTest(accepted=route):
                    self.assertEqual(accepts(route + '~'), accepts(route) + 1)

    def test_patron_observations_preserve_gameplay_and_rng(self):
        with GameRuntime() as game:
            for seed in (123, 42, 2026):
                for enabled, disabled in ((18, 28), (19, 29)):
                    states = []
                    for mode in (enabled, disabled):
                        game.reset()
                        fields, output, code = game.run(['-n', '-Q', '-uPATRON', f'-s{seed}', f'-K{mode}', '-x' + BIRTH + ';2'])
                        self.assertEqual(code, 0, output)
                        self.assertIn('failures 0', output)
                        states.append({k: fields[k] for k in STATE + ('randomness',)})
                        _, output, code = game.run(['-Q', '-uPATRON', '-K4', '-x~'])
                        self.assertEqual(code, 0, output)
                        self.assertIn('failures 0', output)
                        self.assertEqual(len(re.findall(r'^patron-response ', output, re.M)), 7 if mode == enabled else 0)
                    self.assertEqual(*states)
            for mode in (30, 31):
                game.reset()
                _, output, code = game.run(['-n', '-Q', '-uPATRON', '-s123', f'-K{mode}', '-x' + BIRTH + ';2'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                _, output, code = game.run(['-Q', '-uPATRON', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                self.assertRegex(output, r'patron-reason -\d+ Left the quest site')

    def test_dungeon_reward_observation_preserves_gameplay_and_knowledge(self):
        with GameRuntime() as game:
            for seed in (123, 42, 2026):
                baselines = {}
                for mode in (15, 16, 26, 27):  # normal/Instant; observer enabled/disabled
                    game.reset()
                    fields, output, code = game.run(['-n', '-Q', '-uDROPS', f'-s{seed}', f'-K{mode}', '-x' + BIRTH + ';2'])
                    self.assertEqual(code, 0, output)
                    self.assertIn('failures 0', output)
                    state = {k: fields[k] for k in STATE + ('randomness',)}
                    town_mode = mode >= 26
                    if town_mode not in baselines: baselines[town_mode] = state
                    else: self.assertEqual(state, baselines[town_mode])
                    _, output, code = game.run(['-Q', '-uDROPS', '-K4', '-x~'])
                    self.assertEqual(code, 0, output)
                    self.assertIn('quest-receipt 40 ', output)
                    self.assertIn('failures 0', output)
                    self.assertEqual('No floor rewards were visible' in output, mode in (16, 27))
                    if town_mode: self.assertRegex(output, r'quest-response 40 .*increased')
            for mode in (14, 17):  # merge quantities, blind/hallucinating/unseen drops
                game.reset()
                _, output, code = game.run(['-n', '-Q', '-uOBSERVE', '-s123', f'-K{mode}', '-x' + BIRTH + ';2'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)

    def test_patron_histories_and_scroll_memory_survive_load(self):
        def records(output):
            return [s for s in output.splitlines() if s.startswith(('quest-patron ', 'quest-scroll ', 'quest-journal '))]
        with GameRuntime() as game:
            for mode, first, last in ((18, -1, -7), (19, -31, -37)):
                game.reset()
                _, output, code = game.run(['-n', '-Q', '-uPATRON', '-s123', f'-K{mode}', '-x' + BIRTH + ';2'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                baseline, output, code = game.run(['-Q', '-uPATRON', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn(f'quest-patron {first} 4 ', output)
                self.assertIn(f'quest-patron {last} 6 ', output)
                self.assertEqual(len(re.findall(r'^quest-patron ', output, re.M)), 7)
                self.assertRegex(output, r'quest-scroll -\d+ 2 \d+')
                expected = records(output)
                for operation in (5, 4, 5, 4):
                    fields, output, code = game.run(['-Q', '-uPATRON', f'-K{operation}', '-x~'])
                    self.assertEqual(code, 0, output)
                    self.assertIn('failures 0', output)
                    self.assertEqual(records(output), expected)
                    self.assertEqual({k: fields[k] for k in STATE}, {k: baseline[k] for k in STATE})
                # Original patron arrays already existed in v8. Removing the new
                # presentation trailer must not erase those historical assignments.
                save = game.root / 'lib/save/PATRON'
                original = save.read_bytes(); body = decode_save(original)
                memory = body.rfind(b'OQRM'); receipts = body.rfind(b'OQRC')
                legacy = bytearray(body[:memory])
                struct.pack_into('<H', legacy, receipts + 4, 1)
                legacy[-7:] = bytes(7)  # v8 had no synthetic history selections
                save.write_bytes(encode_save(original, legacy, bytes((7, 1, 4, 8))))
                _, output, code = game.run(['-Q', '-uPATRON', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertEqual(len(re.findall(r'^quest-patron ', output, re.M)), 7)
                self.assertNotIn('quest-scroll ', output)
                # Damaged scroll records, with valid outer save checksums.
                patron = body.rfind(b'OQPH')
                self.assertGreater(patron, memory)
                self.assertEqual(struct.unpack_from('<HH', body, patron + 4), (1, 7))
                observations = body[patron:]
                save.write_bytes(original)
                _, output, code = game.run(['-Q', '-uPATRON', '-K5', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertEqual(decode_save(save.read_bytes())[patron:], observations)
                self.assertIn('patron-response ', output)
                # v9 retains its assignment history and scroll memory, without
                # inventing observations that were never saved.
                legacy = encode_save(original, body[:patron], bytes((7, 1, 4, 9)))
                save.write_bytes(legacy)
                _, output, code = game.run(['-Q', '-uPATRON', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertEqual(records(output), expected)
                self.assertNotIn('patron-response ', output)
                self.assertEqual(save.read_bytes(), legacy)
                # Reject invalid/duplicate/nonexistent patron records and
                # overlong/truncated strings, independently of the checksums.
                second = patron + 10
                for _ in range(4):
                    second += 2 + struct.unpack_from('<H', body, second)[0]
                mutations = []
                for offset, fmt, value in [(patron, '<I', 0), (patron + 4, '<H', 99),
                        (patron + 6, '<H', 46), (patron + 8, '<h', 82),
                        (patron + 8, '<h', -30 if mode == 18 else -45),
                        (patron + 10, '<H', 8193), (second, '<h', first)]:
                    damaged = bytearray(body); struct.pack_into(fmt, damaged, offset, value)
                    mutations.append(damaged)
                mutations += [body[:-1], body[:patron + 11]]
                for offset, fmt, value in [(memory + 8, '<h', 32767), (memory + 12, '<H', 0),
                                            (memory + 12, '<H', 1001)]:
                    damaged = bytearray(body); struct.pack_into(fmt, damaged, offset, value)
                    mutations.append(damaged)
                for damaged in mutations:
                    corrupt = encode_save(original, damaged); save.write_bytes(corrupt)
                    _, output, code = game.run(['-Q', '-uPATRON', '-x~  '], timeout=5)
                    self.assertEqual(code, 1, output)
                    self.assertEqual(save.read_bytes(), corrupt)

    def test_town_receipts_survive_reload_and_old_saves_remain_supported(self):
        decode, encode = decode_save, encode_save

        def records(output):
            return [line for line in output.splitlines()
                    if line.startswith(('quest-receipt ', 'quest-response '))]

        with GameRuntime() as game:
            fields, output, code = game.run(['-n', '-Q', '-uRECEIPT', '-s123', '-K3', '-x' + BIRTH + ';2'])
            self.assertEqual(code, 0, output)
            self.assertIn('failures 0', output)
            save = game.root / 'lib/save/RECEIPT'
            original = save.read_bytes()
            body = decode(original)
            self.assertEqual(encode(original, body), original)
            self.assertEqual(original[:4], bytes((7, 1, 4, 10)))
            marker = body.rfind(b'OQRC')
            self.assertGreater(marker, 0)
            self.assertEqual(struct.unpack_from('<HH', body, marker + 4), (2, 2))
            baseline, output, code = game.run(['-Q', '-uRECEIPT', '-K4', '-x~'])
            self.assertEqual(code, 0, output)
            self.assertIn('failures 0', output)
            self.assertIn('quest-receipts 2', output)
            self.assertIn('Outpost pays its debts', output)
            self.assertIn('No item was awarded.', output)
            expected = records(output)
            # Continue and New reuse quest objects in one running executable.
            for script, counts in [('c~^qc~^qq', ['2', '2']),
                                   ('c~^qnn~~^^qq', ['2', '0'])]:
                save.write_bytes(original)
                loaded, output, code = game.run(['-H', '-Q', '-uRECEIPT', '-K6', '-x' + script])
                self.assertEqual(code, 0, output)
                self.assertEqual(re.findall(r'^quest-receipts (\d+)$', output, re.M), counts, output)
                self.assertEqual(loaded['sessions'], '2', output)
                self.assertNotIn('quest-check FAIL', output)
            save.write_bytes(original)
            # Updating a closing speech must not rewrite an already earned one.
            dialogue = game.root / 'lib/edit/q_thieves.txt'
            dialogue.write_text(dialogue.read_text().replace('Outpost pays its', 'CHANGED RESPONSE pays its'))
            for mode in (5, 4, 5, 4):
                loaded, output, code = game.run(['-Q', '-uRECEIPT', '-K%d' % mode, '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                self.assertEqual(records(output), expected)
                self.assertEqual({k: loaded[k] for k in STATE}, {k: baseline[k] for k in STATE})
            # The old layout is precisely the unchanged payload without the
            # new receipt trailer. Also exercise Ormeq's early 0.1 stamp repair.
            for version in (bytes((7, 1, 4, 6)), bytes((0, 1, 4, 6))):
                legacy = encode(original, body[:marker], version)
                save.write_bytes(legacy)
                loaded, output, code = game.run(['-Q', '-uRECEIPT', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('quest-receipts 0', output)
                self.assertIn('failures 0', output)
                self.assertEqual({k: loaded[k] for k in STATE + ('randomness',)},
                                 {k: baseline[k] for k in STATE + ('randomness',)})
                self.assertEqual(save.read_bytes(), legacy)  # reading is not migration
                upgraded, output, code = game.run(['-Q', '-uRECEIPT', '-K5', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertEqual(save.read_bytes()[:4], bytes((7, 1, 4, 10)))
                loaded, output, code = game.run(['-Q', '-uRECEIPT', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('quest-receipts 0', output)  # never invent historical rewards
            # Valid-checksum but invalid records must fail cleanly, not loop or
            # partially accept a damaged save. The original file is preserved.
            mutations = []
            for offset, value in ((marker + 4, 99), (marker + 6, 65535),
                                  (marker + 8, 32767), (marker + 14, 65535)):
                damaged = bytearray(body)
                struct.pack_into('<H', damaged, offset, value)
                mutations.append(bytes(damaged))
            mutations.append(body[:-3])
            for damaged in mutations:
                corrupt = encode(original, damaged)
                save.write_bytes(corrupt)
                loaded, output, code = game.run(['-Q', '-uRECEIPT', '-x~  '], timeout=5)
                self.assertEqual(code, 1, output)
                self.assertEqual(save.read_bytes(), corrupt)
            # New characters in the same installation start without receipts.
            save.write_bytes(original)
            loaded, output, code = game.run(['-n', '-Q', '-uFRESH', '-s42', '-K4', '-x' + BIRTH])
            self.assertEqual(code, 0, output)
            self.assertIn('quest-receipts 0', output)

    def test_failure_records_selection_and_legacy_saves(self):
        def records(output):
            return [line for line in output.splitlines()
                    if line.startswith(('quest-failure ', 'quest-failure-response ', 'quest-journal '))]

        with GameRuntime() as game:
            _, output, code = game.run(['-n', '-Q', '-uFAILURE', '-s123', '-K8', '-x' + BIRTH + ';2'])
            self.assertEqual(code, 0, output)
            self.assertIn('failures 0', output)
            save = game.root / 'lib/save/FAILURE'
            original = save.read_bytes()
            body = decode_save(original)
            marker = body.rfind(b'OQJR')
            receipt_marker = body.rfind(b'OQRC')
            self.assertGreater(marker, receipt_marker)
            self.assertEqual(struct.unpack_from('<HH', body, marker + 4), (1, 4))
            baseline, output, code = game.run(['-Q', '-uFAILURE', '-K4', '-x~'])
            self.assertEqual(code, 0, output)
            self.assertIn('failures 0', output)
            self.assertIn('quest-failures 4', output)
            expected = records(output)
            self.assertTrue(any('Left the quest site' in line for line in expected))
            self.assertTrue(any('Chose to abandon' in line for line in expected))
            self.assertRegex(output, r'quest-journal 2 \d+ 1 \d+')
            # Both repeated loads and a new character in the same process.
            for script, counts in [('c~^qc~^qq', ['4', '4']),
                                   ('c~^qnn~~^^qq', ['4', '0'])]:
                save.write_bytes(original)
                _, output, code = game.run(['-H', '-Q', '-uFAILURE', '-K6', '-x' + script])
                self.assertEqual(code, 0, output)
                self.assertEqual(re.findall(r'^quest-failures (\d+)$', output, re.M), counts, output)
                self.assertNotIn('quest-check FAIL', output)
                if counts[-1] == '0':
                    self.assertEqual(re.findall(r'^quest-journal (.+)$', output, re.M)[-1], '0 0 0 0')
            save.write_bytes(original)
            # Replace all failure dialogue in this disposable installation.
            # Saved responses must remain exact despite future content changes.
            for path in (game.root / 'lib/edit').glob('q_*.txt'):
                text = path.read_text()
                path.write_text(re.sub(r'(?m)^D:.*$', 'D:CHANGED DIALOGUE', text))
            for mode in (5, 4, 5, 4):
                loaded, output, code = game.run(['-Q', '-uFAILURE', '-K%d' % mode, '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                self.assertEqual(records(output), expected)
                self.assertEqual({k: loaded[k] for k in STATE}, {k: baseline[k] for k in STATE})
            # v7 receipts are preserved; v6 and repaired early 0.1 saves have
            # neither trailer. All keep the original gameplay and RNG state.
            for version, end in [((7, 1, 4, 7), marker), ((7, 1, 4, 6), receipt_marker),
                                 ((0, 1, 4, 6), receipt_marker)]:
                legacy_body = bytearray(body[:end])
                if version[-1] == 7:
                    struct.pack_into('<H', legacy_body, receipt_marker + 4, 1)
                legacy = encode_save(original, legacy_body, bytes(version))
                save.write_bytes(legacy)
                loaded, output, code = game.run(['-Q', '-uFAILURE', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('failures 0', output)
                self.assertIn('quest-failures 0', output)
                self.assertIn('quest-journal 0 0 0 0', output)
                self.assertIn('quest-receipts ' + ('1' if version[-1] == 7 else '0'), output)
                self.assertEqual({k: loaded[k] for k in STATE + ('randomness',)},
                                 {k: baseline[k] for k in STATE + ('randomness',)})
                self.assertEqual(save.read_bytes(), legacy)
                _, output, code = game.run(['-Q', '-uFAILURE', '-K5', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertEqual(save.read_bytes()[:4], bytes((7, 1, 4, 10)))
                _, output, code = game.run(['-Q', '-uFAILURE', '-K4', '-x~'])
                self.assertEqual(code, 0, output)
                self.assertIn('quest-failures 0', output)  # no invented history
            # Reject malformed records even with valid checksums: lengths,
            # duplicate identities, booleans, versions, count, tab, selection.
            offsets, pos = [], marker + 8
            for _ in range(4):
                start = pos
                pos += 7  # id, reported flag, reported turn
                for _ in range(2):
                    size = struct.unpack_from('<H', body, pos)[0]
                    pos += 2 + size
                offsets.append((start, pos))
            memory = body.rfind(b'OQRM')
            self.assertEqual(memory - pos, 7)
            # v8 retains failure records and selection but has no scroll block.
            legacy_body = bytearray(body[:memory])
            struct.pack_into('<H', legacy_body, receipt_marker + 4, 1)
            save.write_bytes(encode_save(original, legacy_body, bytes((7, 1, 4, 8))))
            loaded, output, code = game.run(['-Q', '-uFAILURE', '-K4', '-x~'])
            self.assertEqual(code, 0, output)
            self.assertEqual(records(output), expected)
            self.assertEqual({k: loaded[k] for k in STATE + ('randomness',)},
                             {k: baseline[k] for k in STATE + ('randomness',)})
            mutations = []
            for offset, fmt, value in [(marker, '<I', 0), (marker + 4, '<H', 99),
                    (marker + 6, '<H', 65535), (marker + 8, '<h', 32767),
                    (marker + 10, '<B', 2), (marker + 15, '<H', 65535),
                    (pos, '<B', 3), (pos + 1, '<h', 32767),
                    (memory, '<I', 0), (memory + 4, '<H', 99), (memory + 6, '<H', 161),
                    (offsets[1][0], '<h', struct.unpack_from('<h', body, offsets[0][0])[0])]:
                damaged = bytearray(body)
                struct.pack_into(fmt, damaged, offset, value)
                mutations.append(bytes(damaged))
            mutations += [body[:end] for end in (marker + 2, marker + 16, pos + 1, len(body) - 1)]
            for damaged in mutations:
                corrupt = encode_save(original, damaged)
                save.write_bytes(corrupt)
                _, output, code = game.run(['-Q', '-uFAILURE', '-x~  '], timeout=5)
                self.assertEqual(code, 1, output)
                self.assertEqual(save.read_bytes(), corrupt)

    def test_quest_contacts_match_town_plans_and_alternates(self):
        root = Path(__file__).resolve().parent.parent / 'lib/edit'
        aliases = {}
        for block in re.split(r'(?m)(?=^N:)', (root / 'q_info.txt').read_text()):
            quest = re.match(r'N:(\d+):', block)
            alternate = re.search(r'SUBSTITUTE_(\d+)', block)
            if quest and alternate:
                aliases[int(quest[1])] = int(alternate[1])
        sections = re.split(r'(?m)^# (t_\w+\.txt)\n', (root / 'quest_contacts.txt').read_text())
        self.assertEqual(len(sections[1::2]), 9)
        for filename, section in zip(sections[1::2], sections[2::2]):
            town = (root / filename).read_text()
            owners = {int(m[1]): (m[3], m[2]) for m in re.finditer(
                r'^B:(\d+):N:([^:\n]+):([^:\n]+):', town, re.M)}
            expected = {}
            for building, quest in re.findall(r'^L:.:BUILDING_(\d+)\((\d+)\)', town, re.M):
                building, quest = int(building), int(quest)
                if not quest:
                    continue
                expected[quest] = owners[building]
                if quest in aliases:
                    expected[aliases[quest]] = owners[building]
            actual = {}
            for ids, name, building in re.findall(r'^Q:([\d,]+)\nC:([^|]+)\|([^|]+)\|', section, re.M):
                for quest in map(int, ids.split(',')):
                    self.assertNotIn(quest, actual, filename)
                    actual[quest] = (name, building)
            self.assertEqual(actual, expected, filename)

    def test_quest_journal_preserves_rewards_progress_and_rng(self):
        for seed in (123, 42, 2026):
            baseline = None
            with GameRuntime() as game:
                for browse in (0, 1):
                    game.reset()
                    fields, output, code = game.run(['-n', '-Q', '-uQUEST',
                        '-s%d' % seed, '-K%d' % browse, '-x' + BIRTH + ';2'])
                    self.assertEqual(code, 0, output)
                    self.assertIn('failures 0', output)
                    self.assertNotIn('quest-check FAIL', output)
                    self.assertEqual(fields.get('map owner'), '1', output)
                    result = ({k: fields[k] for k in STATE + ('randomness',)},
                              [s for s in output.splitlines() if s.startswith('quest-lifecycle')])
                    if baseline is None:
                        baseline = result
                    else:
                        self.assertEqual(result, baseline)

    def test_map_knowledge_and_rng_isolation(self):
        with GameRuntime() as game:
            fields, output, code = game.run(['-n','-Q','-uMAPTEST','-s123','-J','-x'+BIRTH+';2'])
            self.assertEqual(code,0,output)
            self.assertIn('map-check failures 0',output)
            self.assertNotIn('map-check FAIL',output)


    def test_stack_guard_reads_direct_and_probed_frames(self):
        from check_stack_frames import frames
        assembly = ('small PROC\n sub rsp, 1216\nsmall ENDP\n'
                    'huge PROC\n mov eax, 1469240\n call __chkstk\n'
                    ' sub rsp, rax\nhuge ENDP\n')
        self.assertEqual(list(frames(assembly)), [('small', 1216), ('huge', 1469240)])

    def test_reckoner_oracle_rejects_plausible_but_wrong_numbers(self):
        valid = ('effect-arith -7 0 1 13 0 0 0\n'
                 'effect-arith 19 23 -3 13 0 0 1\n'
                 'effect-check 18 <= 31 1\n'
                 'effect-roll 0 0 99\n'
                 'effect-dice 4 6 12 2 3 1 6\n')
        self.assertEqual(check_reckoner_arithmetic(valid), 5)
        for bad in ('effect-arith 19 24 -3 13 0 0 1',
                    'effect-arith -7 -1 1 13 0 0 0',
                    'effect-check 18 <= 31 0', 'effect-check 4 > 4 1', 'effect-roll 100 0 99',
                    'effect-dice 4 6 12 1 3 1 6'):
            with self.assertRaises(AssertionError, msg=bad):
                check_reckoner_arithmetic(bad)
    def test_live_files_stay_outside_runtime_even_on_failure(self):
        with tempfile.TemporaryDirectory(prefix='ormeq-fixture-') as tmp:
            source = Path(tmp)
            (source / 'ormeq-none.exe').write_bytes(b'fixture executable')
            for name in MUTABLE + ('help', 'edit'):
                folder = source / 'lib' / name
                folder.mkdir(parents=True)
                (folder / 'PLAYER').write_bytes(b'original ' + name.encode())
            before = {p.relative_to(source): p.read_bytes()
                      for p in source.rglob('*') if p.is_file()}
            with self.assertRaisesRegex(RuntimeError, 'simulated failure'):
                with GameRuntime(source) as game:
                    runtime_root = game.root
                    for name in MUTABLE:
                        self.assertEqual(list((game.root / 'lib' / name).iterdir()), [])
                        (game.root / 'lib' / name / 'PLAYER').write_bytes(b'test save')
                    (game.root / 'lib/help/PLAYER').write_bytes(b'generated help')
                    game.reset()
                    self.assertEqual(list((game.root / 'lib/save').iterdir()), [])
                    raise RuntimeError('simulated failure')
            self.assertFalse(runtime_root.exists())
            self.assertEqual(before, {p.relative_to(source): p.read_bytes()
                                      for p in source.rglob('*') if p.is_file()})

    def test_requested_seed_count_is_honoured(self):
        for count in (1, 8, 40):
            with self.subTest(count=count):
                self.assertEqual(len(seed_values(count)), count)
                self.assertEqual(len(set(seed_values(count))), count)
        for count in (0, -1):
            with self.assertRaises(ValueError):
                seed_values(count)


class GameplayTests(unittest.TestCase):
    def test_birth_options_editable_before_and_after_play(self):
        for previous in (False, True):
            # The no-display frontend uses keypad digits, not SDL arrow escapes.
            for keys, enabled in (('6', True), ('t', True), ('684', False)):
                with self.subTest(previous_character=previous, keys=keys):
                    self.game.reset()
                    prefix = 'nn~~^%x' if previous else ''
                    fields = self.play(['-H', '-Q', '-uREGRESSION', '-s42',
                        '-x' + prefix + 'nn=' + keys + '^~~^%xq'])
                    self.assertEqual(fields['sessions'], '2' if previous else '1')
                    name = 'REGRESSION II' if previous else 'REGRESSION'
                    flags = saved_option_flags(self.game.root / 'lib/save' / name)
                    self.assertEqual(bool(flags[6] & (1 << 31)), enabled,
                                     'Easy Identify did not retain the birth choice')
                    if previous:
                        self.assertFalse(saved_option_flags(self.assert_saved())[6] & (1 << 31),
                                         'New birth changed the older character')

    def test_birth_options_lock_after_creation_and_loading(self):
        for enabled in (False, True):
            for keys in ('6', '4', 't'):
                with self.subTest(easy_identify=enabled, keys=keys):
                    self.game.reset()
                    birth = '~n=' + ('t' if enabled else '') + '^~~^'
                    before = self.play(['-n', '-Q', '-uREGRESSION', '-s42',
                                        '-x' + birth + '%s'])
                    expected = saved_option_flags(self.assert_saved())
                    self.assertEqual(bool(expected[6] & (1 << 31)), enabled)
                    self.game.reset()
                    after = self.play(['-n', '-Q', '-uREGRESSION', '-s42',
                                       '-x' + birth + '=b' + keys + '^^%s'])
                    self.assertEqual(saved_option_flags(self.assert_saved()), expected,
                                     'Birth options unlocked after completing birth')
                    self.assertEqual(self.state(before), self.state(after))
                    self.assertEqual(before['randomness'], after['randomness'])
                    loaded = self.play(['-Q', '-uREGRESSION',
                                        '-x~=b' + keys + '^^%s'])
                    self.assertEqual(saved_option_flags(self.assert_saved()), expected,
                                     'Birth options unlocked for a loaded character')
                    self.assertEqual(self.state(after), self.state(loaded))

    def test_cancelled_birth_does_not_unlock_existing_character(self):
        for enabled in (False, True):
            with self.subTest(easy_identify=enabled):
                self.game.reset()
                before = self.play(['-n', '-Q', '-uREGRESSION', '-s42',
                    '-x~n=' + ('t' if enabled else '') + '^~~^%s'])
                expected = saved_option_flags(self.assert_saved())
                # Cancel a second birth after opening its options, then load
                # the original character through the title's Load menu.
                after = self.play(['-H', '-Q', '-uREGRESSION',
                                   '-xnn=^^^la=bt^^%xq'])
                self.assertEqual(saved_option_flags(self.assert_saved()), expected)
                self.assertEqual(self.state(before), self.state(after))

    def test_audio_action_hooks_and_mute_preserve_gameplay(self):
        for seed in (42, 7, 2026):
            baseline = None
            for mode in (2, 3):
                with self.subTest(seed=seed, sound=bool(mode & 1)):
                    self.game.reset()
                    fields, output, code = self.game.run([
                        '-n', '-Q', '-uREGRESSION', '-s%d' % seed,
                        '-A%d' % mode, '-x' + BIRTH + ';2'])
                    self.assertEqual(code, 0, output[-6000:])
                    self.assertEqual(fields.get('audio failures'), '0', output)
                    self.assertGreater(int(fields.get('audio checks', 0)), 1000)
                    state = [line for line in output.splitlines()
                             if line.startswith(('audio-state ', 'audio-projectile '))]
                    self.assertGreater(len(state), 110)
                    final = {k: fields[k] for k in STATE + ('randomness',)}
                    if baseline is None:
                        baseline = state, final
                        self.assertNotIn('audio-event ', output)
                    else:
                        self.assertEqual((state, final), baseline)
                        for cue in ('throw', 'shoot_hit', 'pickup', 'wield', 'quiver',
                                    'stairs_up', 'stairs_down', 'disarm', 'buy', 'sell',
                                    'recover', 'buff_end', 'poisoned', 'paralyzed',
                                    'walk', 'hitwall', 'home_enter', 'store1', 'store_leave',
                                    'ui_accept', 'ui_back'):
                            self.assertIn('audio-event ' + cue + ' ', output)

    def test_audio_preserves_specialised_ranged_combat(self):
        for seed in (42, 7, 2026):
            baseline = None
            for mode in (0, 1):
                with self.subTest(seed=seed, sound=bool(mode)):
                    self.game.reset()
                    fields, output, code = self.game.run([
                        '-n', '-Q', '-uREGRESSION', '-s%d' % seed, '-M10',
                        '-A%d' % mode, '-x' + BIRTH + ';2'])
                    self.assertEqual(code, 0, output[-4000:])
                    self.assertEqual(fields.get('ranged failures'), '0', output[-4000:])
                    steps = [line for line in output.splitlines() if line.startswith('ranged-step ')]
                    self.assertEqual(len(steps), 800)
                    if baseline is None:
                        baseline = steps
                        self.assertNotIn('audio-event ', output)
                    else:
                        self.assertEqual(steps, baseline)
                        self.assertIn('audio-event throw 280', output)
                        self.assertIn('audio-event shoot_hit ', output)

    @classmethod
    def setUpClass(cls):
        cls.game = GameRuntime().__enter__()
        cls.addClassCleanup(cls.game.__exit__, None, None, None)

    def setUp(self):
        self.game.reset()

    def play(self, args):
        fields, output, code = self.game.run(args)
        self.assertEqual(code, 0, output[-6000:])
        self.assertEqual(fields.get('result'), 'alive', output[-6000:])
        self.assertEqual(fields.get('stalls'), '0', output[-6000:])
        for key in STATE:
            self.assertIn(key, fields, 'Missing report field: ' + key)
        return fields

    def new(self, script=WALK_TOWN, extra=(), seed=42):
        return self.play(['-n', '-Q', '-uREGRESSION', '-s%d' % seed,
                          '-x' + BIRTH + script] + list(extra))

    def state(self, fields):
        return {key: fields[key] for key in STATE}

    def assert_saved(self):
        save = self.game.root / 'lib/save/REGRESSION'
        self.assertTrue(save.is_file(), 'Script did not reach the save command')
        self.assertGreater(save.stat().st_size, 100)
        self.assertTrue(save.with_suffix('.who').is_file())
        return save

    def test_identity_rules_match_the_live_roster(self):
        _, output, code = self.game.run(['-I'])
        self.assertEqual(code, 0, output[-6000:])
        live = {'RACE': {}, 'CLASS': {}}
        for line in output.splitlines():
            if line.startswith('identity|'):
                _, kind, idx, name, token = line.split('|')
                live[kind][int(idx)] = (name, token)
        self.assertEqual(len(live['RACE']), 74)
        self.assertEqual(len(live['CLASS']), 52)  # includes Monster, skips retired 12
        for kind, names in catalogue(self.game.root).items():
            self.assertEqual(names, {name for name, _ in live[kind].values()})
        _, errors = check(self.game.root)
        self.assertEqual(errors, [])

        cases = []
        # Every shipped identity condition is tested against every possible
        # value of its variable. This catches both missed recipients and
        # unintended matches (Scarlet Mage used to match ordinary Mage).
        for _, _, kind, values, expression in conditions(self.game.root):
            for idx, (_, token) in live[kind].items():
                race, cls = (idx, 0) if kind == 'RACE' else (0, idx)
                expected = token in values
                equivalent = expression
                if expression.startswith('[NEQ '):
                    expected = not expected
                    # NEQ is dungeon-only syntax; its NOT/EQU equivalent
                    # can be exercised by both production readers.
                    equivalent = '[NOT [EQU ' + expression[5:] + ']'
                cases.append(f'{race} {cls} {int(expected)} {equivalent}')
        # Also cover identities absent from the shipped conditions, including
        # multi-word race names, and nested race/class/level comparisons.
        for race, (_, rt) in live['RACE'].items():
            for cls in (1, 18, 21):  # Mage, Scarlet Mage, Azure Mage
                ct = live['CLASS'][cls][1]
                cases.append(f'{race} {cls} 1 [AND [EQU $RACE {rt}] '
                             f'[EQU $CLASS {ct}] [GEQ $LEVEL 1]]')
                cases.append(f'{race} {cls} 0 [NOT [EQU $CLASS {ct}]]')
        fixture = self.game.root / 'lib/user/identity-cases.txt'
        fixture.write_text('\n'.join(cases) + '\n', encoding='ascii')
        _, output, code = self.game.run(['-E' + str(fixture)], timeout=30)
        self.assertEqual(code, 0, output[-6000:])
        self.assertIn(f'identity checks: {len(cases)} cases, 0 failures', output)
        # Prove the checker rejects the original malformed renames, rather
        # than allowing the legitimate trailing 'Mage' to hide the mistake.
        bad = self.game.root / 'lib/edit/identity-negative.txt'
        bad.write_text('?:[EQU $CLASS Scarlet Mage]\n'
                       '?:[EQU $RACE Vein grazer]\n', encoding='ascii')
        _, errors = check(self.game.root)
        self.assertEqual(len(errors), 3, errors)


    def test_seeded_play_is_repeatable(self):
        first = self.new()
        self.assertGreater(int(first['turns']), 1)
        self.game.reset()
        self.assertEqual(self.state(first), self.state(self.new()))
        self.game.reset()
        self.assertNotEqual(first['world'], self.new(seed=43)['world'])

    def test_town_save_load_and_continue(self):
        saved = self.new(WALK_TOWN + '%s')
        self.assert_saved()
        loaded = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertEqual(self.state(saved), self.state(loaded))
        continued = self.play(['-Q', '-uREGRESSION', '-x~;2;2^q'])
        self.assertGreater(int(continued['turns']), int(saved['turns']))
        self.assertNotEqual(continued['position'], saved['position'])
        self.assertEqual(continued['sessions'], '1')
        reloaded = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertEqual(self.state(continued), self.state(reloaded))

    def test_dungeon_save_load_preserves_floor_and_inventory(self):
        saved = self.new(WALK_TOWN + '^q', extra=('-D1', '-i'))
        self.assertTrue(saved['depth'].startswith('1 in dungeon 1'))
        self.assert_saved()
        loaded = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertEqual(self.state(saved), self.state(loaded))

    def test_early_ormeq_version_stamp_still_loads(self):
        saved = self.new(WALK_TOWN + '%s')
        save = self.assert_saved()
        data = save.read_bytes()
        # Early Ormeq saves used 0.1 with the same payload. Keep the existing
        # loader's version repair covered when changing checksum validation.
        save.write_bytes(bytes([0, 1]) + data[2:])
        loaded = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertEqual(self.state(saved), self.state(loaded))

    def test_death_card_fits_the_headless_terminal(self):
        # Only this temporary character is abandoned. Its 80-column death
        # card formerly inserted a 72-column block at a negative offset.
        fields, output, code = self.game.run(
            ['-n', '-Q', '-uREGRESSION', '-s42', '-x' + BIRTH + 'Qy@~'])
        self.assertEqual(code, 0, output[-6000:])
        self.assertEqual(fields.get('result'), 'dead', output[-6000:])
        self.assertEqual(fields.get('sessions'), '1', output[-6000:])

    def test_pause_settings_return_without_spending_a_turn(self):
        first = self.new('')
        self.game.reset()
        after = self.new('^sxc')
        self.assertEqual(self.state(first), self.state(after))
        self.assertEqual(after['map owner'], '1')
        self.assertEqual(after['knocks'], '0')

    def test_look_navigation_returns_to_map_without_spending_a_turn(self):
        before = self.new('')
        self.game.reset()
        after = self.new('l++--^')
        self.assertEqual(self.state(before), self.state(after))
        self.assertEqual(after['map owner'], '1')
        self.assertEqual(after['knocks'], '0')

    def test_look_card_size_toggle_costs_no_turn_or_randomness(self):
        before = self.new(';2', extra=('-M1',))
        self.game.reset()
        after = self.new(';2l+rR]^', extra=('-M1',))
        self.assertEqual(self.state(before), self.state(after))
        self.assertEqual(before['randomness'], after['randomness'])
        self.assertEqual(after['map owner'], '1')

    def test_warning_border_toggle_persists_without_spending_a_turn(self):
        before = self.new('')
        self.game.reset()
        after = self.new('^sbxc')  # Pause > Settings > warning Border > back
        self.assertEqual(self.state(before), self.state(after))
        prefs = self.game.root / 'lib/user/display.txt'
        self.assertIn('edge_warnings no', prefs.read_text())
        # A fresh process must read 'no', so toggling once writes 'yes'.
        self.new('^sbxc')
        self.assertIn('edge_warnings yes', prefs.read_text())

    def test_ascii_floor_suboption_persists_and_costs_no_turns(self):
        before = self.new('')
        self.game.reset()
        after = self.new('^siiaxc') # Settings: ASCII -> Hybrid -> Full Tile, toggle floors
        self.assertEqual(self.state(before),self.state(after))
        self.assertEqual(before['randomness'],after['randomness'])
        prefs=self.game.root/'lib/user/display.txt'
        self.assertIn('ascii_floors yes',prefs.read_text())
        self.new('^siiiaxc') # Cycle through the other modes, then toggle off
        self.assertIn('ascii_floors no',prefs.read_text())

    def test_start_screen_can_save_continue_and_save_again(self):
        fields = self.play(['-H', '-Q', '-uREGRESSION', '-s42',
                            '-xnn~~^;2;2^qc;6;6^qq'])
        self.assertEqual(fields['sessions'], '2')
        self.assertGreater(int(fields['turns']), 20)
        self.assert_saved()
        loaded = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertEqual(self.state(fields), self.state(loaded))

    def test_start_screen_can_create_another_character(self):
        fields = self.play(['-H', '-Q', '-uREGRESSION', '-s42',
                            '-xnn~~^;2;2;6;6^qnn~~^^qq'])
        self.assertEqual(fields['sessions'], '2')
        self.assertEqual(fields['turns'], '1')
        self.assertEqual(fields['level'], '1')
        self.assert_saved()
        newer = self.game.root / 'lib/save/REGRESSION II'
        self.assertTrue(newer.is_file())
        older = self.play(['-Q', '-uREGRESSION', '-x~'])
        self.assertGreater(int(older['turns']), int(fields['turns']))

    def test_new_characters_preserve_existing_saves_and_number_successively(self):
        self.new(WALK_TOWN + '%s')
        original = self.assert_saved().read_bytes()
        # Explicit -u and the ordinary New entry must both preserve the file.
        self.play(['-H', '-Q', '-uREGRESSION', '-s123', '-xnn~~^^qq'])
        second = self.game.root / 'lib/save/REGRESSION II'
        self.assertTrue(second.is_file())
        before_second = second.read_bytes()
        self.play(['-H', '-Q', '-uREGRESSION II', '-s7', '-xnn~~^^qq'])
        self.assertTrue((self.game.root / 'lib/save/REGRESSION III').is_file())
        self.assertEqual(self.assert_saved().read_bytes(), original)
        self.assertEqual(second.read_bytes(), before_second)

    def test_new_character_names_handle_long_and_aliased_paths(self):
        cases = [('PLAYER', 'PLAYER', 'PLAYER II'),
                 ('Hyddyddin IX', 'Hyddyddin IX', 'Hyddyddin X'),
                 ('Hyddyddin 9', 'Hyddyddin 9', 'Hyddyddin 10'),
                 ('abcdefghijklmnopqrstuv', 'abcdefghijklmnopqrstuv', 'abcdefghijklmnopqrs II'),
                 ('abcdefghijklmnopqr III', 'abcdefghijklmnopqr III', 'abcdefghijklmnopqr IV'),
                 ('Name 99999999999999999', 'Name 99999999999999999', 'Name 99999999999999 II')]
        if os.name == 'nt':
            cases += [('Hyddyddin', 'hyddyddin', 'Hyddyddin II'),
                      ('A?B', 'A_B', 'A_B II'), ('A/B', 'A_B', 'A_B II'),
                      ('A\\B', 'A_B', 'A_B II'), ('Hyddyddin.', 'Hyddyddin', 'Hyddyddin. II')]
        for name, occupied, expected in cases:
            with self.subTest(name=name):
                self.game.reset()
                saved = self.game.root / 'lib/save' / occupied
                saved.write_bytes(b'Existing save sentinel, even if corrupt')
                self.play(['-n', '-Q', '-u'+name, '-s42', '-x'+BIRTH+'%s'])
                self.assertEqual(saved.read_bytes(), b'Existing save sentinel, even if corrupt')
                self.assertTrue((saved.parent / expected).is_file(), list(saved.parent.iterdir()))

    def test_new_character_skips_many_names_and_recovery_files(self):
        folder = self.game.root / 'lib/save'
        for i in range(1,63): (folder / ('Chain %d' % i)).write_bytes(b'preserve')
        for suffix in ('.old', '.new', '.lok', '.who'):
            (folder / ('Recovery'+suffix)).write_bytes(b'preserve')
        before = {p.name:p.read_bytes() for p in folder.iterdir()}
        self.play(['-n', '-Q', '-uChain 1', '-s42', '-x'+BIRTH+'%s'])
        self.assertTrue((folder/'Chain 63').is_file())
        self.play(['-n', '-Q', '-uRecovery', '-s42', '-x'+BIRTH+'%s'])
        self.assertTrue((folder/'Recovery II').is_file())
        for name,data in before.items(): self.assertEqual((folder/name).read_bytes(), data)

    def test_typed_name_and_default_player_cannot_replace_a_save(self):
        folder = self.game.root / 'lib/save'
        (folder/'PLAYER').write_bytes(b'preserve default')
        (folder/'Hyddyddin').write_bytes(b'preserve typed')
        # No -u: default PLAYER is occupied. Then type an occupied name on
        # the race/class screen (Ctrl+U clears the input's current contents).
        self.play(['-n', '-Q', '-s42', '-x~nn%uHyddyddin~~~^%s'])
        self.assertTrue((folder/'Hyddyddin II').is_file())
        self.assertEqual((folder/'Hyddyddin').read_bytes(), b'preserve typed')
        self.play(['-n', '-Q', '-s42', '-x'+BIRTH+'%s'])
        self.assertTrue((folder/'PLAYER II').is_file())
        self.assertEqual((folder/'PLAYER').read_bytes(), b'preserve default')

    def test_name_collision_changes_no_gameplay_or_randomness(self):
        before = self.new('')
        (self.game.root/'lib/save/REGRESSION').write_bytes(b'preserve')
        after = self.new('')
        self.assertEqual(self.state(before), self.state(after))
        self.assertEqual(before['randomness'], after['randomness'])

    def test_three_new_characters_in_one_session_keep_separate_files(self):
        fields = self.play(['-H', '-Q', '-uREGRESSION', '-s42',
                            '-xnn~~^^qnn~~^^qnn~~^^qq'])
        self.assertEqual(fields['sessions'], '3')
        for name in ('REGRESSION','REGRESSION II','REGRESSION III'):
            self.assertTrue((self.game.root/'lib/save'/name).is_file())

    def test_directory_with_character_name_is_not_a_free_save_path(self):
        folder = self.game.root/'lib/save/Blocked'
        folder.mkdir()
        (folder/'keep').write_bytes(b'preserve')
        self.play(['-n','-Q','-uBlocked','-s42','-x'+BIRTH+'%s'])
        self.assertTrue((folder.parent/'Blocked II').is_file())
        self.assertEqual((folder/'keep').read_bytes(), b'preserve')

    def test_truncated_save_is_rejected_and_preserved(self):
        self.new(WALK_TOWN + '%s')
        save = self.assert_saved()
        original = save.read_bytes()
        damaged_files = [original[:n] for n in (0, 3, 8, len(original) // 2,
                                               len(original) - 1)]
        damaged_files.append(original[:100] + bytes([original[100] ^ 1]) + original[101:])
        for damaged in damaged_files:
            with self.subTest(length=len(damaged)):
                save.write_bytes(damaged)
                fields, output, code = self.game.run(
                    ['-Q', '-uREGRESSION', '-x~  '], timeout=5)
                self.assertEqual(code, 1, output[-4000:])
                self.assertNotEqual(fields.get('result'), 'alive', output)
                self.assertEqual(save.read_bytes(), damaged)

    def test_melee_recording_and_display_preserve_real_attacks(self):
        baseline = None
        for recording, mode in ((0, 'off'), (1, 'off'), (1, 'static'), (1, 'animated')):
            with self.subTest(recording=recording, mode=mode):
                self.game.reset()
                (self.game.root / 'lib/user/display.txt').write_text(
                    'calculations ' + mode + '\n', encoding='ascii')
                fields, output, code = self.game.run([
                    '-n', '-Q', '-uREGRESSION', '-s42', '-M%d' % recording,
                    '-x' + BIRTH + ';5'])
                self.assertEqual(code, 0, output[-4000:])
                self.assertEqual(fields.get('melee failures'), '0', output[-4000:])
                steps = [line for line in output.splitlines() if line.startswith('melee-step ')]
                self.assertEqual(len(steps), 90, output[-4000:])
                if baseline is None:
                    baseline = steps
                else:
                    self.assertEqual(steps, baseline)  # HP, energy, RNG, statuses, XP and skill
                self.assertEqual(fields['melee history'], '48' if recording else '0')
                if recording:
                    values = fields['melee coverage'].split()
                    coverage = dict(zip(values[::2], map(int, values[1::2])))
                    for kind, count in coverage.items():
                        self.assertGreater(count, 0, kind + ' was not exercised')

    def test_calculation_history_and_replay_cost_no_turns(self):
        ordinary = self.new(';2', extra=('-M1',))
        self.game.reset()
        inspected = self.new(';2%p%irnp\\]\\[%i%ie^x', extra=('-M1',))
        self.assertEqual(self.state(ordinary), self.state(inspected))
        self.assertEqual(ordinary['melee history'], inspected['melee history'])
        self.assertEqual(inspected['map owner'], '1')
        self.assertEqual(ordinary['randomness'], inspected['randomness'])

    def test_calculation_export_preserves_historical_knowledge(self):
        for older, concealed in ((0, False), (44, True)):
            with self.subTest(concealed=concealed):
                self.game.reset()
                fields = self.new(';2%p%i' + 'n' * older +
                                  'e|working.txt~^^x', extra=('-M1',))
                exported = (self.game.root / 'lib/user/working.txt').read_text()
                self.assertIn('Past Reckonings', exported)
                if concealed:
                    self.assertIn('Unidentified equipment', exported)
                    self.assertNotIn('Damage dice:', exported)
                    self.assertNotIn('Critical draw:', exported)
                    self.assertNotIn('Attack draw', exported)
                else:
                    self.assertIn('Damage dice: 2d6 = 4 + 1 = 5.', exported)
                    self.assertIn('Other melee bonuses: 9 +3 = 12.', exported)
                self.assertEqual(fields['map owner'], '1')

    def test_past_reckonings_list_account_scroll_and_help_cost_no_turns(self):
        before = self.new(';2', extra=('-M1',))
        self.game.reset()
        # List -> account -> scroll -> list -> another account -> replay,
        # switch tabs twice, inspect exact working/help, then return to play.
        after = self.new(';2%p%i~  ln~r%i%ie^?^x', extra=('-M1',))
        self.assertEqual(self.state(before), self.state(after))
        self.assertEqual(before['randomness'], after['randomness'])
        self.assertEqual(after['map owner'], '1')

    def test_past_reckonings_scroll_reaches_the_last_calculation_line(self):
        fields, output, code = self.game.run([
            '-n', '-Q', '-v', '-uREGRESSION', '-s42', '-M1',
            '-x' + BIRTH + ';2%p%i~' + ' ' * 10])
        self.assertEqual(code, 0, output[-4000:])
        self.assertIn('Recorded arithmetic joins', output)
        self.assertIn('[E Exact/export]', output)

    def test_extended_melee_stages_preserve_real_attacks(self):
        baseline = None
        for recording in (2, 3):
            self.game.reset()
            fields, output, code = self.game.run([
                '-n', '-Q', '-uREGRESSION', '-s42', '-M%d' % recording,
                '-x' + BIRTH + ';2'])
            self.assertEqual(code, 0, output[-4000:])
            self.assertEqual(fields['melee failures'], '0', output[-4000:])
            steps = [line for line in output.splitlines() if line.startswith('melee-step ')]
            self.assertEqual(len(steps), 90)
            if baseline is None:
                baseline = steps
            else:
                self.assertEqual(steps, baseline)
                values = fields['melee stages'].split()
                for kind, count in zip(values[::2], map(int, values[1::2])):
                    self.assertGreater(count, 0, kind + ' was not exercised')

    def test_spells_unarmed_and_incoming_preserve_resolution(self):
        baseline = None
        for recording, mode in ((4, 'off'), (5, 'off'), (5, 'static'), (5, 'animated')):
            with self.subTest(recording=recording, mode=mode):
                self.game.reset()
                (self.game.root / 'lib/user/display.txt').write_text('calculations ' + mode + '\n')
                fields, output, code = self.game.run([
                    '-n', '-Q', '-uREGRESSION', '-s42', '-M%d' % recording,
                    '-x' + BIRTH + ';2'])
                self.assertEqual(code, 0, output[-4000:])
                self.assertEqual(fields.get('reckoning failures'), '0', output[-4000:])
                self.assertEqual(fields.get('camera failures'), '0', output[-4000:])
                steps = [s for s in output.splitlines() if s.startswith('reckoning-step ')]
                self.assertEqual(len(steps), 170, output[-4000:])
                if baseline is None:
                    baseline = steps
                else:
                    self.assertEqual(steps, baseline)
                    values = fields['reckoning coverage'].split()
                    for kind, count in zip(values[::2], map(int, values[1::2])):
                        self.assertGreater(count, 0, kind + ' was not exercised')

    def test_spell_sources_preserve_resolution_and_explain_each_target(self):
        baseline = None
        for recording, mode in ((6, 'off'), (7, 'off'), (7, 'static'), (7, 'animated')):
            with self.subTest(recording=recording, mode=mode):
                self.game.reset()
                (self.game.root / 'lib/user/display.txt').write_text('calculations ' + mode + '\n')
                fields, output, code = self.game.run([
                    '-n', '-Q', '-uREGRESSION', '-s42', '-M%d' % recording,
                    '-x' + BIRTH + ';2'])
                self.assertEqual(code, 0, output[-5000:])
                self.assertEqual(fields.get('spell failures'), '0', output[-5000:])
                steps = [s for s in output.splitlines() if s.startswith('spell-step ')]
                self.assertEqual(len(steps), 195, output[-5000:])
                if baseline is None:
                    baseline = steps
                else:
                    self.assertEqual(steps, baseline)
                    values = fields['spell coverage'].split()
                    for kind, count in zip(values[::2], map(int, values[1::2])):
                        self.assertGreater(count, 0, kind + ' was not exercised')

    def test_specialist_spells_saves_and_status_arithmetic(self):
        for seed in (42, 7, 2026):
            baseline = None
            for recording, mode in ((8, 'off'), (9, 'off'), (9, 'static'), (9, 'animated')):
                with self.subTest(seed=seed, recording=recording, mode=mode):
                    self.game.reset()
                    (self.game.root / 'lib/user/display.txt').write_text('calculations ' + mode + '\n')
                    fields, output, code = self.game.run([
                        '-n', '-Q', '-uREGRESSION', '-s%d' % seed, '-M%d' % recording,
                        '-x' + BIRTH + ';2'], timeout=60)
                    self.assertEqual(code, 0, output[-4000:])
                    self.assertEqual(fields.get('effect failures'), '0', output[-4000:])
                    steps = [s for s in output.splitlines() if s.startswith('effect-step ')]
                    self.assertEqual(len(steps), 1428)
                    if baseline is None:
                        baseline = [s for s in output.splitlines() if s.startswith('effect-step')]
                    else:
                        self.assertEqual([s for s in output.splitlines() if s.startswith('effect-step')], baseline)
                        self.assertGreater(check_reckoner_arithmetic(output), 1000)
                        values = fields['effect coverage'].split()
                        for label, count in zip(values[::2], map(int, values[1::2])):
                            self.assertGreater(count, 0, label)

    def test_message_tabs_and_empty_history_cost_no_turns(self):
        before = self.new('')
        self.game.reset()
        after = self.new('%p%i%i%i^')
        self.assertEqual(self.state(before), self.state(after))
        self.assertEqual(before['randomness'], after['randomness'])
        self.assertEqual(after['map owner'], '1')

    def test_projectile_calculations_preserve_gameplay(self):
        for seed in (42, 7, 2026):
            baseline = None
            for recording, mode in ((10, 'off'), (11, 'off'), (11, 'static'), (11, 'animated')):
                with self.subTest(seed=seed, recording=recording, mode=mode):
                    self.game.reset()
                    (self.game.root / 'lib/user/display.txt').write_text('calculations ' + mode + '\n')
                    fields, output, code = self.game.run([
                        '-n', '-Q', '-uREGRESSION', '-s%d' % seed, '-M%d' % recording,
                        '-x' + BIRTH + ';2'], timeout=60)
                    self.assertEqual(code, 0, output[-4000:])
                    self.assertEqual(fields.get('ranged failures'), '0', output[-4000:])
                    steps = [s for s in output.splitlines() if s.startswith('ranged-step ')]
                    self.assertEqual(len(steps), 800)
                    if baseline is None:
                        baseline = steps
                    else:
                        self.assertEqual(steps, baseline)
                        self.assertGreater(check_reckoner_arithmetic(output), 2000)
                        values = fields['ranged coverage'].split()
                        for label, count in zip(values[::2], map(int, values[1::2])):
                            self.assertGreater(count, 0, label)

    def test_incoming_checks_and_prevention_preserve_gameplay(self):
        for seed in (42, 7, 2026):
            baseline = None
            for recording, mode in ((12, 'off'), (13, 'off'), (13, 'static'), (13, 'animated')):
                with self.subTest(seed=seed, recording=recording, mode=mode):
                    self.game.reset()
                    (self.game.root / 'lib/user/display.txt').write_text('calculations ' + mode + '\n')
                    fields, output, code = self.game.run([
                        '-n', '-Q', '-uREGRESSION', '-s%d' % seed, '-M%d' % recording,
                        '-x' + BIRTH + ';2'], timeout=60)
                    self.assertEqual(code, 0, output[-4000:])
                    self.assertEqual(fields.get('defence failures'), '0', output[-4000:])
                    steps = [s for s in output.splitlines() if s.startswith('defence-step ')]
                    self.assertEqual(len(steps), 1800)
                    if baseline is None:
                        baseline = steps
                    else:
                        self.assertEqual(steps, baseline)
                        self.assertGreater(check_reckoner_arithmetic(output), 4000)
                        values = fields['defence coverage'].split()
                        for label, count in zip(values[::2], map(int, values[1::2])):
                            self.assertGreater(count, 0, label)

    def test_review_privacy_menus_and_provenance(self):
        for seed in (42, 7, 2026):
            expected = None
            for recording in (14, 15):
                self.game.reset()
                fields, output, code = self.game.run([
                    '-n', '-Q', '-uREGRESSION', '-s%d' % seed,
                    '-M%d' % recording, '-x' + BIRTH + ';2'], timeout=60)
                self.assertEqual(code, 0, output[-4000:])
                self.assertEqual(fields.get('review failures'), '0', output[-4000:])
                steps = [line for line in output.splitlines()
                         if line.startswith(('review-step ', 'review-menu '))]
                self.assertEqual(len(steps), 241)
                if expected is None:
                    expected = steps
                else:
                    self.assertEqual(steps, expected)
                    self.assertGreater(check_reckoner_arithmetic(output), 100)

    def test_history_numbers_follow_records_when_scrolling(self):
        import re
        rows = []
        for navigation in ('', 'nnnn'):
            self.game.reset()
            fields, output, code = self.game.run([
                '-n', '-Q', '-v', '-uREGRESSION', '-s42', '-M1',
                '-x' + BIRTH + ';2%p%i' + navigation])
            self.assertEqual(code, 0, output[-4000:])
            numbers = [int(n) for n in re.findall(r'[#](\d+) ', output)]
            self.assertGreater(len(numbers), 2, output[-5000:])
            self.assertGreater(min(numbers), 9)
            self.assertEqual(numbers, list(range(numbers[0], numbers[0]-len(numbers), -1)))
            rows.append(numbers)
        self.assertLess(rows[1][0], rows[0][0])
        self.assertTrue(set(rows[0]) & set(rows[1]))

    def test_history_tabs_preserve_selected_attack_and_blow(self):
        for switch in ('', '%i%i'):
            self.game.reset()
            fields, output, code = self.game.run([
                '-n', '-Q', '-v', '-uREGRESSION', '-s42', '-M1',
                '-x' + BIRTH + ';2%p%in\\]' + switch])
            self.assertEqual(code, 0, output[-4000:])
            self.assertIn('Recent 2/48', output)
            self.assertRegex(output, r'2 of \d+ results')

    def test_export_requires_confirmation_before_replacing_a_file(self):
        marker = 'Previous export: keep this unless confirmed.\n'
        for answer in ('n', 'y'):
            self.game.reset()
            export = self.game.root / 'lib/user/past-reckonings.txt'
            export.write_text(marker)
            fields, output, code = self.game.run([
                '-n', '-Q', '-uREGRESSION', '-s42', '-M1',
                '-x' + BIRTH + ';2%p%ie|~' + answer + '^^'])
            self.assertEqual(code, 0, output[-4000:])
            if answer == 'n':
                self.assertEqual(export.read_text(), marker)
            else:
                self.assertNotEqual(export.read_text(), marker)
                self.assertIn('Past Reckonings', export.read_text())

    def test_location_survives_sidebar_redraw(self):
        fields, output, code = self.game.run([
            '-n', '-Q', '-v', '-uREGRESSION', '-s42', '-x' + BIRTH + '%r'])
        self.assertEqual(code, 0, output[-4000:])
        self.assertRegex(output, r'(?m)^19\|.* Outpost[ ]*$')
        self.assertEqual(fields['turns'], '1')

    def test_live_calculation_setting_is_remembered(self):
        self.new('^scx^')
        pref = self.game.root / 'lib/user/display.txt'
        self.assertIn('calculations static', pref.read_text())
        self.new('^scx^')
        self.assertIn('calculations animated', pref.read_text())
        self.new('^scx^')
        self.assertIn('calculations off', pref.read_text())

    def test_three_dungeon_floors_can_be_visited(self):
        sweep = Sweep(False, self.game)
        fields = sweep.check('three floors',
                             ['-n', '-uREGRESSION', '-s42', '-i', '-D1',
                              '-N3', '-T40', '-r', WALK],
                             want_depth=3, want_dungeon=1, want_floors=3)
        self.assertEqual(sweep.failures, [])
        self.assertEqual(fields['floors'], '3')
        self.assertTrue(fields['depth'].startswith('3 in dungeon 1'))
        self.assertGreater(int(fields['turns']), 80)


if __name__ == '__main__':
    unittest.main(verbosity=2)
