"""Check race/class rule tokens and body/guild references against shipped help.

Gameplay tests also compare this catalogue with the initialized C definitions.
This is a reference check, not a lore or originality verdict.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
IDENTITY = re.compile(r'<topic:([^>]+)><color:o>')
CONDITION = re.compile(r'\[(?:EQU|NEQ) \$(RACE|CLASS) ([^\[\]]+)\]')


def catalogue(root=ROOT):
    def names(file):
        return set(IDENTITY.findall((root / 'lib/help' / file).read_text(
            encoding='latin-1')))
    return {'RACE': names('Races.txt') | names('MonsterRaces.txt'),
            'CLASS': names('Classes.txt') | {'Monster'}}


def conditions(root=ROOT):
    for folder in ('lib/edit', 'lib/pref'):
        for path in sorted((root / folder).glob('*')):
            if path.is_file():
                for number, line in enumerate(path.read_text(
                        encoding='latin-1').splitlines(), 1):
                    if line.startswith('?:'):
                        for match in CONDITION.finditer(line):
                            yield path, number, match[1], match[2].split(), match[0]


def check(root=ROOT):
    names = catalogue(root)
    tokens = {kind: {name.replace(' ', '-') for name in values}
              for kind, values in names.items()}
    errors = []
    count = 0
    for path, number, kind, values, _ in conditions(root):
        count += 1
        for value in values:
            if value not in tokens[kind]:
                errors.append(f'{path.relative_to(root)}:{number}: '
                              f'unknown ${kind} token {value!r}')
    for path in sorted((root / 'lib/edit').glob('*.txt')):
        for number, line in enumerate(path.read_text(encoding='latin-1').splitlines(), 1):
            body = re.fullmatch(r'P:Copy:([^:]+):([^:]+)', line)
            guild = re.fullmatch(r'B:\d+:C:([^:]+):[^:]+', line)
            refs = [('RACE', body[1]), ('CLASS', body[2])] if body else []
            if guild and guild[1] != '*':
                refs.append(('CLASS', guild[1]))
            for kind, value in refs:
                count += 1
                if value not in names[kind]:
                    errors.append(f'{path.relative_to(root)}:{number}: '
                                  f'unknown {kind.lower()} name {value!r}')
    return count, errors


if __name__ == '__main__':
    count, errors = check()
    for error in errors:
        print(error)
    print(f'identity references: {count} checked, {len(errors)} failures')
    sys.exit(bool(errors))
