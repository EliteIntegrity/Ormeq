#!/usr/bin/env python
"""Play the game a great many times and report what fell over.

The headless build (build.ps1 -Frontend none) plays from a keystroke script
and prints where it got to. This runs it over and over -- every depth, every
dungeon, a spread of seeds -- and says nothing at all unless something goes
wrong, which is the only way a soak test is any use.

    python tools/soak.py                 the standard sweep
    python tools/soak.py --seeds 40      more seeds, same sweeps
    python tools/soak.py --only dungeons just the one sweep
    python tools/soak.py -v              print every run, not just failures

A run has failed if the program crashed, if it never printed a report, if it
did not reach the floor it was told to, or if it stopped moving. Dying is not
a failure: a first level character on floor fifty is supposed to die.
"""
import argparse
import io
import os
import re
import sys
import time

from game_runtime import GameRuntime

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(HERE, 'ormeq-none.exe')

# A short walk in every direction. It repeats (-r), so it does not need to be
# long -- it needs to be varied, or the character walks into the same wall.
WALK = '-x~n~~;2;2;2;3;3;9;9;8;8;8;7;7;1;1;4;4;4;6;6;6'


def seed_values(count):
    """Keep the original smoke seeds, then add distinct seeds up to count."""
    if count < 1:
        raise ValueError('seed count must be positive')
    values = [1, 2, 3, 7, 42, 99, 1234, 31337]
    candidate = 1
    while len(values) < count:
        if candidate not in values:
            values.append(candidate)
        candidate += 1
    return values[:count]


def depth_of(fields):
    m = re.match(r'^(\d+) in dungeon (\d+)', fields.get('depth', ''))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


class Sweep(object):
    def __init__(self, verbose, runtime):
        self.verbose = verbose
        self.runtime = runtime
        self.failures = []
        self.runs = 0

    def check(self, what, args, want_depth=None, want_dungeon=None,
              want_floors=None):
        self.runtime.reset()
        fields, out, code = self.runtime.run(args, timeout=300)
        self.runs += 1
        got_d, got_y = depth_of(fields)
        why = []

        if code != 0:
            why.append('exit %d' % code)
        if fields.get('result') not in ('alive', 'dead'):
            why.append('no playable character reported')
        if want_depth is not None and got_d != want_depth:
            why.append('wanted floor %d, got %s' % (want_depth, got_d))
        if want_dungeon is not None and got_y != want_dungeon:
            why.append('wanted dungeon %d, got %s' % (want_dungeon, got_y))
        if want_floors is not None and int(fields.get('floors', 0)) < want_floors:
            why.append('only %s floors of %d' % (fields.get('floors'), want_floors))
        if int(fields.get('stalls', 0)) > 0:
            why.append('stopped moving %s times' % fields.get('stalls'))

        if why:
            self.failures.append((what, ' and '.join(why), out))
            print('  FAILED  %-34s %s' % (what, ' and '.join(why)))
        elif self.verbose:
            print('  ok      %-34s turns %-6s depth %-4s %s' % (
                what, fields.get('turns'), got_d, fields.get('result')))
        return fields


def sweep_depths(s):
    """Can the game build a floor at every depth there is?"""
    print('every depth, floor 1 to 127')
    for d in list(range(1, 21)) + list(range(25, 128, 5)) + [127]:
        s.check('floor %d' % d,
                ['-s1', '-uSOAK', '-n', '-i', '-D%d' % d, WALK],
                want_depth=d, want_dungeon=1)


def sweep_dungeons(s):
    """Can it build the first floor of every dungeon in the game?"""
    print('every dungeon, at its own shallowest floor')
    cur = None
    for line in io.open(os.path.join(HERE, 'lib/edit/d_info.txt'),
                        encoding='latin-1'):
        if line.startswith('N:'):
            p = line.rstrip().split(':', 2)
            cur = (int(p[1]), p[2])
        elif line.startswith('W:') and cur:
            num, name = cur
            mind = int(line.rstrip()[2:].split(':')[0])
            cur = None
            if not num:
                continue
            s.check('%d %s' % (num, name),
                    ['-s1', '-uSOAK', '-n', '-i', '-D%d' % mind, '-Y%d' % num,
                     WALK],
                    want_depth=mind, want_dungeon=num)


def sweep_dives(s, seeds):
    """Can it actually go down, floor after floor, on a fresh game?"""
    print('%d games, each diving as far as it lives' % len(seeds))
    for seed in seeds:
        f = s.check('seed %d diving' % seed,
                    ['-s%d' % seed, '-uSOAK', '-n', '-i', '-D1', '-N127',
                     '-T150', '-r', WALK],
                    want_floors=10)
        if not s.verbose and f.get('result'):
            print('  seed %-7d floor %-5s %s' % (
                seed, depth_of(f)[0],
                f.get('killed by') and 'killed by ' + f['killed by']
                or f.get('result')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--only', choices=['depths', 'dungeons', 'dives'])
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()

    if not os.path.exists(EXE):
        print('No headless build. Make one with:')
        print('    ./build.ps1 -Frontend none')
        return 2

    if a.seeds < 1:
        ap.error('--seeds must be positive')
    began = time.time()
    with GameRuntime() as runtime:
        print('Private runtime: %s' % runtime.root)
        s = Sweep(a.verbose, runtime)
        if a.only in (None, 'depths'):
            sweep_depths(s)
        if a.only in (None, 'dungeons'):
            sweep_dungeons(s)
        if a.only in (None, 'dives'):
            sweep_dives(s, seed_values(a.seeds))

    print()
    print('%d games in %.1f seconds' % (s.runs, time.time() - began))
    if not s.failures:
        print('nothing fell over')
        return 0

    print('%d fell over:' % len(s.failures))
    for what, why, out in s.failures:
        print()
        print('  %s -- %s' % (what, why))
        for line in out.splitlines()[-24:]:
            print('    | ' + line)
    return 1


if __name__ == '__main__':
    sys.exit(main())
