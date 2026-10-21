"""Check that each option has a help entry in lib/help/option.txt.

Parse C strings with escaped quotes before matching option names."""
import io
import re
import sys


def strings_in(line):
    """Every C string literal on a line, escaped quotes respected."""
    out, i = [], 0
    while True:
        i = line.find('"', i)
        if i < 0:
            return out
        j, buf = i + 1, []
        while j < len(line) and line[j] != '"':
            if line[j] == '\\':
                j += 1
            buf.append(line[j] if j < len(line) else '')
            j += 1
        out.append(''.join(buf))
        i = j + 1


def options():
    """(variable, internal name, label) for every live option_info entry."""
    src = io.open('src/tables.c', encoding='latin-1').read()
    a = src.index('option_type option_info[]')
    b = src.index('\n};', a)
    lines = [l for l in src[a:b].split('\n') if not l.lstrip().startswith('//')]

    out, pending = [], None
    for line in lines:
        m = re.search(r'\{\s*&(\w+),\s*(?:TRUE|FALSE),\s*OPT_PAGE_\w+', line)
        if m:
            pending = m.group(1)
        if pending:
            found = strings_in(line)
            if len(found) >= 2:
                out.append((pending, found[0], found[1]))
                pending = None
    return out


def named_topics():
    """Topics the code asks for by hand, rather than through option_info.

    The screens for the delay factor, the two warnings and the window flags
    name their help entry in a string literal. Two of those literals were
    wrong -- one case error and one topic that had never been written -- and
    nothing noticed, because a help topic that does not exist simply shows
    nothing.
    """
    out = []
    for name in ('src/cmd4.c',):
        src = io.open(name, encoding='latin-1').read()
        out += re.findall(r'doc_display_help\(\s*"option\.txt"\s*,\s*"([^"]+)"',
                          src)
        out += re.findall(r'_digit_setting\([^;]*?"([A-Za-z]+)"\s*,\s*&',
                          src, re.S)
    return sorted(set(out))


def main():
    help_text = io.open('lib/help/option.txt', encoding='latin-1').read()
    topics = set(re.findall(r'<topic:([^>]+)>', help_text))

    named = [t for t in named_topics() if t not in topics]
    if named:
        print('%d topic%s named in the source but not in option.txt:'
              % (len(named), '' if len(named) == 1 else 's'))
        for t in named:
            print('  ' + t)
        return 1

    opts = options()
    missing = [o for o in opts if o[1] not in topics]

    if missing:
        print('%d option%s with nothing written about them:'
              % (len(missing), '' if len(missing) == 1 else 's'))
        for var, name, label in missing:
            print('  %-24s %s' % (name, label))
        return 1

    print('%d options and %d named topics -- every one has an entry in'
          ' option.txt' % (len(opts), len(named_topics())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
