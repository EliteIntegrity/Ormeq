"""Run the checks shipped with the source distribution: python tools/check_public.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
CHECKS = (
    'check_refs.py', 'check_identity_rules.py', 'check_objects.py',
    'check_options.py', 'check_encoding.py', 'check_layout.py', 'check_links.py',
    'check_release_metadata.py', 'check_audio.py', 'check_sprite_pack.py',
)


def main():
    failed = []
    for name in CHECKS:
        result = subprocess.run([sys.executable, str(ROOT / 'tools' / name)], cwd=ROOT)
        if result.returncode:
            failed.append(name)
    if failed:
        print('Failed: ' + ', '.join(failed))
        return 1
    print('%d public data checks passed.' % len(CHECKS))
    return 0


if __name__ == '__main__':
    sys.exit(main())
