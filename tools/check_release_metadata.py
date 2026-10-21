"""Keep public documentation tied to the version compiled into Ormeq."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent


def main():
    defines = (ROOT / 'src/defines.h').read_text(encoding='latin-1')
    match = re.search(r'^#define ORMEQ_VER\s+"(\d+\.\d+\.\d+)"', defines, re.M)
    if not match:
        print('ORMEQ_VER must contain major.minor.patch.')
        return 1
    version = match[1]
    claims = {'README.md': '**Version ' + version + ' is',
              'RELEASE_NOTES.md': '# Ormeq ' + version + '\n'}
    for name, expected in claims.items():
        if expected not in (ROOT / name).read_text(encoding='utf-8'):
            print('%s does not describe version %s.' % (name, version))
            return 1
    print('Release documentation matches Ormeq ' + version + '.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
