#!/usr/bin/env python
"""Check named monster, artefact, dungeon and quest-file references.

Matches the game's case-insensitive lookup rules. Ambiguous substring goals
are warnings because the game resolves them to the first matching record.
Run: python tools/check_refs.py"""

import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EDIT = os.path.join(ROOT, 'lib', 'edit')

# A bare data-file name, for reading the Makefile's install list.
NAME_TXT = r'([A-Za-z0-9_]+\.txt)'

# A quoted name is how the data files write a monster whose name contains a
# comma -- MON("fang, farmer bulley's dog", CLONE) -- so the quotes must be
# matched before the bare form, or the pattern stops at that comma and hands
# back a token beginning with a quote, which resolves() then waves through as
# "not a name at all". One reference had been broken that way for a whole
# rename pass: the Cloning Pits asked for Farmer Maggot's dog long after
# Maggot became Bulley, and nothing said so.
MON = re.compile(r'MON\(\s*"([^"]+)"|MON\(([^),]+)')
KILL = re.compile(r'G:KILL\(([^),]+)')
FIND = re.compile(r'G:FIND\(([^),]+)')
ART = re.compile(r'ART\(([^),]+)')
OBJ = re.compile(r'OBJ\(([^),]+)')
EGO = re.compile(r'EGO\(([^),]+)')

KEYWORD = re.compile(r'^[A-Z_0-9]+$')


def is_keyword(tok):
    """MON() also takes summon types and room themes -- ANIMAL, HI_DRAGON --
    which are parser keywords in init1.c, not names in r_info.txt. They are
    always upper case and they are not ours to check."""
    return KEYWORD.match(tok) is not None


def normalise(text):
    """k_info writes names with plural and article markers -- "& Potion~ of
    Life" -- and the lookups do not. Strip the decoration from both sides."""
    return ' '.join(text.lower().replace('&', ' ').replace('~', ' ')
                    .replace('#', ' ').replace('|', ' ').split())


def names_in_order(filename):
    """The same names, in the order the file has them.

    parse_lookup_monster() walks r_info.txt from the top and takes the
    first substring match, so which one a goal resolves to is a fact
    about file order. A set cannot answer that question.
    """
    out = []
    for line in io.open(os.path.join(EDIT, filename), encoding='latin-1'):
        if line.startswith('N:'):
            parts = line.rstrip().split(':', 2)
            if len(parts) == 3 and parts[2].strip():
                out.append(normalise(parts[2]))
    return out


def names_in(filename):
    out = set()
    for line in io.open(os.path.join(EDIT, filename), encoding='latin-1'):
        if line.startswith('N:'):
            parts = line.rstrip('\r\n').split(':', 2)
            if len(parts) == 3 and parts[2].strip():
                out.add(normalise(parts[2]))
    return out


def resolves(tok, pool, drop_leading_word=False):
    key = normalise(tok.strip('^$ '))
    if not key or tok.strip()[0] in '*[<"':
        return True                      # a wildcard, or not a name at all
    if key.isdigit() or is_keyword(tok.strip()):
        return True                      # an index, or a parser keyword
    if key in pool:
        return True
    if any(key in name for name in pool):      # the parser takes a partial
        return True
    if not drop_leading_word:
        return False
    # An artefact may be asked for by its whole display name -- "bow of Bard",
    # "boots of the Bard" -- where a_info only holds the "of ..." half.
    #
    # Artefacts ONLY. Dropping the first word and matching the rest is far too
    # generous for a creature: it let MON(dark elven lord) resolve against
    # "dusk elven lord" on the strength of "elven lord", which is precisely
    # the rename this check exists to catch. A checker that shares the blind
    # spot of the thing it checks is not a check.
    parts = key.split()
    return len(parts) > 1 and any(' '.join(parts[1:]) in name for name in pool)


# Quest files that exist on purpose with no F: line pointing at them.
# Anything else unreferenced is a leftover, and three of them had been sitting
# in lib/edit for years: the pumpkin quest, the snarl master, and the first
# draft of the vapor quest, every one superseded upstream and never removed.
UNREFERENCED_OK = {
    'q_info.txt':
        'the index itself, which is the thing doing the referencing',
    'q_removed.txt':
        'a deliberate stub. Its own header explains it: a quest removed in an '
        'active release should not break existing games, so this is here to '
        'be pointed at, and being unpointed-at is its normal state',
}


def _quest_file_hygiene():
    """Both directions, and the install list with them.

    lib/edit/Makefile is an install list that none of our three builds reads,
    and it had rotted both ways at once -- naming four files that were gone
    and missing five that were there, one of them a rename of mine that fixed
    q_info.txt and nothing else. An unused list that is wrong is worse than no
    list, because the next person to use it will believe it.
    """
    bad = []

    disk = sorted(f for f in os.listdir(EDIT) if f.endswith('.txt'))
    quests = [f for f in disk if f.startswith('q_')]

    referenced = set()
    for line in io.open(os.path.join(EDIT, 'q_info.txt'), encoding='latin-1'):
        if line.startswith('F:'):
            referenced.add(line[2:].strip())

    for f in quests:
        if f in referenced or f in UNREFERENCED_OK:
            continue
        bad.append('lib/edit/%s -- no F: line in q_info.txt names it' % f)

    mk = os.path.join(EDIT, 'Makefile')
    if os.path.exists(mk):
        body = io.open(mk, encoding='latin-1').read().split('CONFIG =', 1)[-1]
        listed = set(re.findall(NAME_TXT, body))
        for f in sorted(listed - set(disk)):
            bad.append('lib/edit/Makefile lists %s, which is not there' % f)
        for f in sorted(set(disk) - listed):
            bad.append('lib/edit/Makefile does not list %s' % f)

    return bad, len(quests)


def main():
    mon = names_in('r_info.txt')
    art = names_in('a_info.txt')
    dun = names_in('d_info.txt')
    obj = names_in('k_info.txt')
    ego = names_in('e_info.txt')

    bad = []
    quest_files = 0
    for path in sorted(glob.glob(os.path.join(EDIT, '*.txt'))):
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        for n, line in enumerate(io.open(path, encoding='latin-1'), 1):
            line = line.rstrip('\r\n')
            for m in MON.finditer(line):
                tok = (m.group(1) or m.group(2)).strip()
                if is_keyword(tok) or resolves(tok, mon):
                    continue
                bad.append((rel, n, 'MON', tok))
            # Rewards and floor contents are looked up by name too, and these
            # fail quietly: a broken ART() is a quest that hands out nothing.
            # OBJ() is deliberately not checked: a base object is named by
            # its type and its sval together -- OBJ(potion of life) against a
            # k_info entry that is only called "Life" -- so a name-only check
            # cannot tell a broken one from a working one.
            for pat, pool, label in ((ART, art, 'ART'), (EGO, ego, 'EGO')):
                for m in pat.finditer(line):
                    tok = m.group(1).strip()
                    if resolves(tok, pool, drop_leading_word=True):
                        continue
                    bad.append((rel, n, label, tok))
            if os.path.basename(path) != 'q_info.txt':
                continue
            # KILL, FIND and W:dungeon are q_info.txt's alone. In a_info,
            # e_info and f_info a W: line is weight, depth and rarity, and
            # checking those against dungeon names produced twenty thousand
            # false alarms the first time round.
            for m in KILL.finditer(line):
                tok = m.group(1).strip()
                if resolves(tok, mon):
                    continue
                bad.append((rel, n, 'KILL', tok))
            for m in FIND.finditer(line):
                tok = m.group(1).strip()
                if resolves(tok, art, drop_leading_word=True):
                    continue
                bad.append((rel, n, 'FIND', tok))
            if line.startswith('W:'):
                tok = line[2:].strip()
                if resolves(tok, dun):
                    continue
                bad.append((rel, n, 'W', tok))
            # A quest's prose lives in its own file, named here. Nothing
            # notices when that file is missing -- the quest simply has
            # nothing to say -- so a renamed quest file is a quest that goes
            # quiet. Same shape as the sixty-six broken ART() rewards.
            if line.startswith('F:'):
                tok = line[2:].strip()
                quest_files += 1
                if tok and os.path.exists(os.path.join(EDIT, tok)):
                    continue
                bad.append((rel, n, 'F', tok))

    # A duplicate name is an ambiguous reference, because every lookup above
    # is by name. These two were in the tree before we touched it.
    ALLOWED = ('Metal Babble', 'The Quicksilver Imp', 'Novice mage',
               'Novice warrior', 'Novice paladin', 'Novice ranger',
               'Novice mindcrafter', 'of Bard', 'of Aripert', 'of Destruction',
               'of Musashi', 'of Wilhelm Tell', 'of Yositsune',
               "of Shiva's Avatar", 'of the Ending')
    for label, filename in (('monster', 'r_info.txt'), ('artefact', 'a_info.txt'),
                            ('dungeon', 'd_info.txt')):
        seen = {}
        for n, line in enumerate(io.open(os.path.join(EDIT, filename),
                                         encoding='latin-1'), 1):
            if not line.startswith('N:'):
                continue
            parts = line.rstrip().split(':', 2)
            if len(parts) != 3 or not parts[2].strip() or parts[2] == '*':
                continue
            if parts[2] in ALLOWED:
                continue
            if parts[2] in seen:
                bad.append((filename, n, 'duplicate ' + label,
                            parts[2] + ' -- also line %d' % seen[parts[2]]))
            seen[parts[2]] = n

    # Ambiguity: resolves, but perhaps not to what was meant. See the note
    # at the top -- the game takes the first match in file order.
    warn = []
    ordered = {'KILL': names_in_order('r_info.txt'),
               'FIND': names_in_order('a_info.txt')}
    for n, line in enumerate(io.open(os.path.join(EDIT, 'q_info.txt'),
                                     encoding='latin-1'), 1):
        for pat, label in ((KILL, 'KILL'), (FIND, 'FIND')):
            for m in pat.finditer(line):
                tok = normalise(m.group(1).strip().strip('^$ '))
                if not tok or is_keyword(m.group(1).strip()):
                    continue
                hits = [name for name in ordered[label] if tok in name]
                if len(hits) > 1:
                    warn.append((n, label, tok, hits))

    if warn:
        print('check_refs: %d goal(s) match more than one name. The game '
              'takes the first:' % len(warn))
        for n, label, tok, hits in warn:
            print('  q_info.txt:%d  %s(%s) -> %s' % (n, label, tok, hits[0]))
            for h in hits[1:4]:
                print('        also matches %s' % h)

    stray, _n = _quest_file_hygiene()

    if not bad and not stray:
        print('check_refs: %d monsters, %d artefacts, %d dungeons, '
              '%d quest files -- every reference resolves, and every quest '
              'file is referenced'
              % (len(mon), len(art), len(dun), quest_files))
        return 0

    if stray:
        print('check_refs: %d quest file(s) nothing reads:' % len(stray))
        for line in stray:
            print('  %s' % line)
        if not bad:
            return 1
        print()

    print('check_refs: %d unresolved reference(s):' % len(bad))
    for rel, n, kind, tok in bad:
        print('  %s:%d  %s(%s)' % (rel, n, kind, tok))
    return 1


if __name__ == '__main__':
    sys.exit(main())
