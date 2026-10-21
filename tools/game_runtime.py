"""Private runtime copies for headless tests. Never move or restore live saves."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
MUTABLE = ('save', 'user', 'apex', 'bone', 'data')


class GameRuntime:
    def __init__(self, source=ROOT):
        self.source = Path(source).resolve()
        self.root = None
        self._temp = None

    def __enter__(self):
        self._temp = tempfile.TemporaryDirectory(prefix='ormeq-test-')
        self.root = Path(self._temp.name).resolve()
        try:
            # Real copies: generated help and cache writes must be private too.
            shutil.copytree(self.source / 'lib', self.root / 'lib',
                            ignore=lambda path, names: MUTABLE
                            if Path(path) == self.source / 'lib' else ())
            shutil.copy2(self.source / 'ormeq-none.exe', self.root)
            self.reset()
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def reset(self):
        """Start a fresh character environment within this private installation."""
        for name in MUTABLE:
            target = self.root / 'lib' / name
            if target.resolve().parent != self.root / 'lib':
                raise RuntimeError('Unexpected runtime directory: ' + str(target))
            if target.exists():
                shutil.rmtree(target)
            target.mkdir()

    def run(self, args, timeout=60):
        try:
            process = subprocess.run(
                [str(self.root / 'ormeq-none.exe')] + list(args),
                cwd=str(self.root), capture_output=True, text=True,
                errors='replace', timeout=timeout)
            output = process.stdout + process.stderr
            code = process.returncode
        except subprocess.TimeoutExpired as exc:
            chunks = [exc.stdout or b'', exc.stderr or b'']
            output = ''.join(x.decode(errors='replace') if isinstance(x, bytes)
                             else x for x in chunks)
            return {}, output + '\nRun timed out.', -999
        fields = {}
        for line in output.splitlines():
            match = re.match(r'^([a-z ]+?)\s\s+(.*)$', line)
            if match:
                fields[match[1].strip()] = match[2].strip()
        return fields, output, code

    def __exit__(self, *exc):
        if self._temp:
            # TemporaryDirectory created this direct child; never accept a
            # caller-supplied cleanup path or a link to another installation.
            if (self.root.parent != Path(tempfile.gettempdir()).resolve()
                    or self.root.is_symlink()
                    or not self.root.name.startswith('ormeq-test-')):
                raise RuntimeError('Unexpected temporary root: ' + str(self.root))
            self._temp.cleanup()
            self._temp = None
