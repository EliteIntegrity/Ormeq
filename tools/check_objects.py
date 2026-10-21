"""Check OBJ() references against composed object-kind names.

Uses the same prefixes and substring lookup as _lookup_kind() in init1.c.
A failed lookup places no object in a quest reward or layout."""
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDIT = os.path.join(ROOT, 'lib', 'edit')

TV_SCROLL = 70
TV_POTION = 75
TV_FOOD = 80
SV_FOOD_MAX_MUSHROOM = 31

OBJ = re.compile(r'OBJ\(([^),]+)')
KEYWORD = re.compile(r'^[A-Z_0-9]+$')


def prep_name_aux(src):
    """_prep_name_aux() in init1.c, exactly: drop a leading run of ' &[(',
    drop every ~ ] ), lower-case the rest."""
    i = 0
    while i < len(src) and src[i] in ' &[(':
        i += 1
    return ''.join(c.lower() for c in src[i:] if c not in '~])')


def kinds():
    """The composed names, in file order, the way _lookup_kind() builds them."""
    out = []
    name = tval = sval = None

    for line in io.open(os.path.join(EDIT, 'k_info.txt'), encoding='latin-1'):
        line = line.rstrip('\r\n')
        if line.startswith('N:'):
            if name is not None and tval is not None:
                out.append(compose(name, tval, sval))
            parts = line.split(':')
            # N:index:name  or  N:*:name:flavour
            name = parts[2] if len(parts) > 2 else ''
            tval = sval = None
        elif line.startswith('I:') and name is not None:
            parts = line.split(':')
            if len(parts) > 2:
                try:
                    tval, sval = int(parts[1]), int(parts[2])
                except ValueError:
                    tval = sval = None

    if name is not None and tval is not None:
        out.append(compose(name, tval, sval))
    return out


def compose(name, tval, sval):
    if tval == TV_FOOD and sval is not None and sval <= SV_FOOD_MAX_MUSHROOM:
        buf = '^mushroom of '
    elif tval == TV_POTION:
        buf = '^potion of '
    elif tval == TV_SCROLL:
        buf = '^scroll of '
    else:
        buf = '^'
    return buf + prep_name_aux(name) + '$'


def main():
    pool = kinds()
    bad = []

    for path in sorted(glob.glob(os.path.join(EDIT, '*.txt'))):
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        for n, line in enumerate(io.open(path, encoding='latin-1'), 1):
            for m in OBJ.finditer(line):
                arg = m.group(1).strip()
                # A wildcard, a type keyword, an effect keyword or a raw
                # index -- none of those are names and none are ours.
                if arg == '*' or KEYWORD.match(arg) or arg.isdigit():
                    continue
                if arg.startswith('<'):          # a template placeholder
                    continue
                if any(arg in name for name in pool):
                    continue
                bad.append((rel, n, arg))

    if not bad:
        print('check_objects: %d object kinds -- every OBJ() by name resolves'
              % len(pool))
        return 0

    print('check_objects: %d OBJ() reference(s) match no object. Each one '
          'places nothing, silently:' % len(bad))
    for rel, n, arg in bad:
        print('  %s:%d  OBJ(%s)' % (rel, n, arg))
    return 1


if __name__ == '__main__':
    sys.exit(main())
