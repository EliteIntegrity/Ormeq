"""Check MSVC x64 /FAs listings produced by ORMEQ_STACK_AUDIT.

Usage: python tools/check_stack_frames.py out-none/frames
Keep Debug listings: Release can optimize away dangerously large temporaries.
"""
from pathlib import Path
import re
import sys


def frames(text):
    for name, body in re.findall(r'^([^\s;]+) PROC\b(.*?)^\S+\s+ENDP',
                                 text, re.M | re.S):
        # Ignore source comments; the prologue either subtracts a constant or
        # loads it into EAX and calls the Windows stack-probe helper.
        prologue = '\n'.join(line for line in body.splitlines()[:40]
                             if not line.lstrip().startswith(';'))
        sizes = re.findall(r'\bsub\s+rsp,\s*(\d+)', prologue)
        sizes += re.findall(r'\bmov\s+eax,\s*(\d+)\s*[^\n]*\n\s*call\s+__chkstk',
                            prologue)
        yield name, max(map(int, sizes), default=0)


def check(directory):
    found = []
    for path in Path(directory).glob('*.asm'):
        found.extend((size, path.stem, name)
                     for name, size in frames(path.read_text(errors='replace')))
    if not any(file == 'do-spell' and name == 'do_armageddon_spell'
               for size, file, name in found):
        raise ValueError('Missing Armageddon listing; configure ORMEQ_STACK_AUDIT and build Debug.')
    for size, file, name in found:
        # Inherited save decoder has one fixed 64 KiB input buffer. This narrow
        # exception cannot excuse growth in spell functions or other callers.
        limit = 66000 if (file, name) == ('load', 'rd_savefile_new_aux') else 65536
        if size > limit:
            raise ValueError(f'{file}:{name}: {size:,} byte frame exceeds {limit:,}')
    print(f'Checked {len(found)} function frames; Armageddon: '
          f'{next(size for size, file, name in found if name == "do_armageddon_spell"):,} bytes.')


if __name__ == '__main__':
    check(sys.argv[1])
