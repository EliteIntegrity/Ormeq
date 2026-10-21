"""Check help links against their target files and topic anchors.

Reads lib/help/*.txt markup. The generated plain-text mirror has no links."""
import glob
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

LINK = re.compile(r'<link:([^>#]+)(?:#([^>]+))?>')
TOPIC = re.compile(r'<topic:([^>]+)>')


def main():
    files = sorted(glob.glob(os.path.join(ROOT, 'lib', 'help', '*.txt')))
    if not files:
        print('no help files found')
        return 1

    bodies = {}
    for path in files:
        bodies[os.path.basename(path)] = io.open(
            path, encoding='latin-1', errors='replace').read()

    topics = {name: set(TOPIC.findall(body)) for name, body in bodies.items()}

    broken = []
    total = 0
    for name in sorted(bodies):
        for n, line in enumerate(bodies[name].splitlines(), 1):
            for m in LINK.finditer(line):
                total += 1
                target, anchor = m.group(1), m.group(2)
                if target not in bodies:
                    broken.append((name, n, m.group(0), 'no such help file'))
                elif anchor and anchor not in topics[target]:
                    near = sorted(t for t in topics[target]
                                  if anchor.lower() in t.lower()
                                  or t.lower() in anchor.lower())
                    why = 'no <topic:%s> in %s' % (anchor, target)
                    if near:
                        why += ' -- did you mean %s?' % ', '.join(near)
                    broken.append((name, n, m.group(0), why))

    if broken:
        print('%d help link%s go%s nowhere:'
              % (len(broken), '' if len(broken) == 1 else 's',
                 'es' if len(broken) == 1 else ''))
        print()
        for name, n, link, why in broken:
            print('  lib/help/%s:%d' % (name, n))
            print('      %-44s %s' % (link, why))
        print()
        print('The ? key fetches these. A link that resolves to nothing tells '
              'the player nothing about why.')
        return 1

    print('%d help links across %d files -- every one resolves'
          % (total, len(bodies)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
