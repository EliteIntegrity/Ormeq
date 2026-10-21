"""Check sidebar alignment in lib/file/news*.txt splash screens.

The bottom status line is independent of the sidebar and is excluded.
Use --all to show every measured row."""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = os.path.join(ROOT, 'lib', 'file')

TAG = re.compile(r'<[^>]*>')

# The labels these screens use down the right-hand side. Both cases: some
# screens write STR! and some write Str!.
LABEL = re.compile(
    r'(LEVEL|Level|EXP|Exp|AU|STR|Str|INT|Int|WIS|Wis|DEX|Dex|CON|Con|'
    r'CHR|Chr|AC|HP|SP|Fast|Slow|Study|Poison|Blind|Conf|Afraid|Stun|Cut|'
    r'Full|Hungry|Weak|Berserk|Hero|Bless|Prot|Invuln|Rflct|Wraith|Recall)'
    r'[!:]*\s')

# "The Sink: L55", "Ormeq 0.1.howe" -- the status line and the title.
STATUS = re.compile(r"[A-Z][A-Za-z' ]*: L\d+")

# A sidebar sits in the right-hand half. Anything left of this is prose.
LEFT_EDGE = 40

# There is deliberately no "and not too far out" guard here.
#
# The first version had one -- ignore anything more than ten columns from
# the main column, on the reasoning that a bigger gap meant a different
# element. It let a fifteen-column break through and reported the file
# square, which is worse than not checking it. The bottom status line is
# excluded by STATUS above, by what it *is* rather than by how far away it
# happens to sit, and that is the only exclusion this needs.


def vis(line):
    """What the player sees: the markup removed."""
    return TAG.sub('', line).rstrip('\r')


def sidebar_col(v):
    """Visible column this line's sidebar starts at, or None."""
    if STATUS.match(v):
        return None
    for m in LABEL.finditer(v):
        c = m.start()
        if c >= LEFT_EDGE and (c == 0 or v[c - 1] == ' '):
            return c
    return None


# How much clear space the sidebar wants between itself and a sentence.
#
# Only a sentence. The **map** rows are drawn to a fixed width and the
# sidebar sits directly after them with a single space, by design -- the
# first version of this rule flagged a hundred and fifty rows of dungeon
# floor. Two is the floor for prose: news8 came out of a repair with one,
# and "Vulmurn, the High Captain of Emberlords LEVEL 50" reads as a
# collision however square the column is.
MIN_GAP = 2

WORD = re.compile(r'[A-Za-z]{2,}')


def is_prose(left):
    """Three real words means a sentence; #%.ddd means the map.

    A run of letters is not enough: a room full of dogs draws as "ddddddd"
    and a vault wall as "DDDDDDDDDD", and the first version of this counted
    both as words. A real word has a vowel in it and is not one letter
    repeated.
    """
    return len(_real(left)) >= 3


def _real(text):
    return [w for w in WORD.findall(text)
            if set(w.lower()) & set('aeiou') and len(set(w.lower())) > 1]


def is_word(tail):
    """Is the thing abutting the sidebar a word, or is it art?"""
    return bool(_real(tail))


def tight(path, main_col):
    """Prose lines whose text runs up against the sidebar."""
    out = []
    for i, line in enumerate(io.open(path, encoding='latin-1')):
        v = vis(line)
        if sidebar_col(v) != main_col:
            continue
        left = v[:main_col]
        # What matters is what is *touching* the sidebar, not what is on
        # the line somewhere. news9 ends a UI row with "xxxxxxxxxx" and had
        # a sentence earlier on the same line; the x's are art and abut the
        # column by design.
        if not left.strip() or not is_prose(left):
            continue
        if not is_word(left.rstrip()[-12:]):
            continue
        gap = len(left) - len(left.rstrip())
        if gap < MIN_GAP:
            out.append((i + 1, gap, left.rstrip()[-34:]))
    return out


def main():
    show_all = '--all' in sys.argv
    screens = sorted(f for f in os.listdir(FILES)
                     if re.match(r'news\d*\.txt$', f))
    ragged, checked, rows = [], 0, 0

    for f in screens:
        cols = {}
        for i, line in enumerate(
                io.open(os.path.join(FILES, f), encoding='latin-1')):
            c = sidebar_col(vis(line))
            if c is not None:
                cols.setdefault(c, []).append((i + 1, vis(line)[c:c + 24]))
        if not cols:
            continue
        main_col = max(cols, key=lambda c: len(cols[c]))
        if len(cols[main_col]) < 3:
            continue                       # not a sidebar layout
        checked += 1
        rows += sum(len(v) for v in cols.values())

        stray = [(c, r) for c, r in sorted(cols.items()) if c != main_col]

        # A square column is not the whole of it. Padding a line back into
        # line can leave the prose butting straight up against the sidebar
        # with one space between them, which reads as badly as a ragged
        # column and passes a column check. The gap is part of the layout.
        for ln, gap, txt in tight(os.path.join(FILES, f), main_col):
            stray.append((main_col, [(ln, '%d space%s before it: %s'
                                      % (gap, '' if gap == 1 else 's', txt))]))

        if stray:
            ragged.append((f, main_col, len(cols[main_col]), stray))
        elif show_all:
            print('  %-14s sidebar at column %d, %d rows, square'
                  % (f, main_col, len(cols[main_col])))

    if not ragged:
        print('%d splash screens, %d sidebar rows -- every column square'
              % (checked, rows))
        return 0

    print('%d splash screen%s ha%s a sidebar out of true:'
          % (len(ragged), '' if len(ragged) == 1 else 's',
             's' if len(ragged) == 1 else 've'))
    for f, main_col, n, stray in ragged:
        print()
        print('  %s -- sidebar at column %d on %d rows' % (f, main_col, n))
        for c, entries in stray:
            for ln, txt in entries:
                print('    line %-3d column %-3d (%+d)  %r'
                      % (ln, c, c - main_col, txt.rstrip()))
    print()
    print('A rename changed the length of something on the left of these '
          'lines. Padding may not be enough: shorten the prose.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
