"""Check source and game-data encoding against documented exceptions.

The English game data uses ASCII. Source and inherited attribution files
have explicitly listed exceptions. Use --all to report every affected line."""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Check game data and source; Markdown documentation permits Unicode.
DIRS = ('lib', 'src')

SKIP = ('lib/save', 'lib/data', 'lib/apex', 'lib/bone', 'lib/user', 'lib/art',
        '__pycache__',

        # Upstream's macOS bundle. We build with MSVC and have never built
        # it; three of its four non-ASCII files are compiled binary bundle
        # resources (a .nib is a serialised object graph, a .strings is
        # UTF-16), and the fourth is a copyright string with a (c) sign in
        # it, which is the one place a (c) sign belongs. Nothing in here is
        # prose anybody reads. check_names covers its file names.
        'src/cocoa')

# Binary by design. Checking bytes for high bits in a PNG is not a check.
BINARY = ('.png', '.art', '.ico', '.icns', '.wav', '.ogg', '.ttf', '.fon',
          '.res', '.rc', '.bmp', '.dll', '.exe', '.pdf', '.ods')

ALLOWED = {
    # The auto-pickup sample configuration, inherited and inert: an
    # English-only build never matches a Japanese item name, so nothing in
    # it can fire. It is a worked example of the pickup syntax rather than
    # data, it names no creature and no place, and rewriting a hundred item
    # names to demonstrate a syntax nobody reads in English is work with no
    # reader. Left, listed, and re-read on 7 September 2026.
    'lib/pref/picktype.prf': 'Japanese auto-pickup sample, no names in it',
}


def _show(line):
    """The offending bytes, as bytes.

    Printing them decoded needs a console that can render them, and the
    console here is cp1252 -- the first version of this crashed on its own
    output. Escaping is also the more honest report: \\x83\\x82 is exactly
    what every other checker in this directory sees.
    """
    out = []
    for b in bytearray(line.rstrip(b'\r')):
        out.append(chr(b) if 32 <= b < 127 else '\\x%02x' % b)
    s = ''.join(out)
    return s[:70] + (' ...' if len(s) > 70 else '')


def _files():
    out = []
    for d in DIRS:
        for dp, dn, fn in os.walk(os.path.join(ROOT, d)):
            rel_dir = os.path.relpath(dp, ROOT).replace(os.sep, '/')
            if any(rel_dir == x or rel_dir.startswith(x + '/') for x in SKIP):
                dn[:] = []
                continue
            for f in fn:
                if f.lower().endswith(BINARY):
                    continue
                p = os.path.join(dp, f)
                out.append(os.path.relpath(p, ROOT).replace(os.sep, '/'))
    return sorted(out)


def main():
    show_all = '--all' in sys.argv
    hits = {}
    checked = 0

    for rel in _files():
        if rel in ALLOWED:
            continue
        checked += 1
        try:
            raw = io.open(os.path.join(ROOT, *rel.split('/')), 'rb').read()
        except IOError:
            continue
        if not any(b > 127 for b in bytearray(raw)):
            continue
        for n, line in enumerate(raw.split(b'\n'), 1):
            if any(b > 127 for b in bytearray(line)):
                hits.setdefault(rel, []).append((n, _show(line)))

    if not hits:
        print('%d shipped files checked, all of them ASCII '
              '(%d allowed by name)' % (checked, len(ALLOWED)))
        return 0

    total = sum(len(v) for v in hits.values())
    print('%d line%s in %d file%s is not ASCII, and nothing we own can read '
          'it:' % (total, '' if total == 1 else 's',
                   len(hits), '' if len(hits) == 1 else 's'))
    for rel in sorted(hits):
        rows = hits[rel]
        print()
        print('  %s  (%d)' % (rel, len(rows)))
        for n, text in (rows if show_all else rows[:6]):
            print('    %-6d %s' % (n, text))
        if not show_all and len(rows) > 6:
            print('    ... and %d more' % (len(rows) - 6))

    print()
    print('An owned name written in another script passes every other check '
          'in this directory.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
