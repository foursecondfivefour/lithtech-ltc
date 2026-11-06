#!/usr/bin/env python3
"""Build helper: resolve the MSVC-era backslash and case-insensitive includes.

Compiles one TU, and whenever gcc reports a missing header, looks for a file with
that name (case-insensitively ignored) anywhere under src/ and links it into
compat/ under the exact spelling the source asks for.  Repeat until the TU
compiles or the iteration budget is exhausted.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, 'src')
COMPAT = os.path.join(ROOT, 'compat')

# every header we have, indexed by basename in lower case
INDEX = {}
for dirpath, dirnames, filenames in os.walk(SRC):
    dirnames[:] = [d for d in dirnames if d != 'compat']
    for name in filenames:
        full = os.path.join(dirpath, name)
        if os.path.islink(full):      # никогда не резолвим в собственный симлинк
            continue
        INDEX.setdefault(name.lower(), full)


def make_link(wanted):
    """Create compat/<wanted> for a header the sources ask for."""
    os.makedirs(COMPAT, exist_ok=True)
    # MSVC paths use backslashes, which gcc keeps as literal filename characters
    normalised = wanted.replace('\\', '/')
    target = INDEX.get(os.path.basename(normalised).lower())
    if target is None:
        return False
    link = os.path.join(COMPAT, wanted)
    parent = os.path.dirname(link)
    if parent:
        os.makedirs(parent, exist_ok=True)
    if os.path.lexists(link):
        os.unlink(link)
    # относительный симлинк: репозиторий остаётся переносимым
    os.symlink(os.path.relpath(target, COMPAT), link)
    print('  compat: %-40s -> %s' % (wanted, os.path.relpath(target, ROOT)))
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    cmd = sys.argv[1:]
    for _ in range(80):
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode == 0:
            return 0

        out = proc.stdout + proc.stderr
        missing = re.search(r'fatal error: ([^:]+): No such file or directory', out)
        if not missing:
            # a real compile error, not a missing header
            sys.stderr.write(out)
            return proc.returncode

        if not make_link(missing.group(1)):
            sys.stderr.write('cannot resolve header %r\n%s' % (missing.group(1), out))
            return 1

    print('iteration budget exhausted')
    return 1


if __name__ == '__main__':
    sys.exit(main())
