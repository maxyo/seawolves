#!/usr/bin/env python3
"""Host check of the engine build ID the script cache is keyed on (src/libs/util/include/platform/module_build_id.hpp,
src/libs/core/src/compiler.cpp CalculateCacheKey): what it reads at run time is the linker's GNU build ID of the
binary (readelf), the same for an identical rebuild, different when the code changes; libc's is found too."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]


def build(directory, name, variant):
    binary = Path(directory) / name
    subprocess.run(
        [
            os.environ.get('CXX', 'c++'),
            '-std=c++20',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-O1',
            f'-DVARIANT={variant}',
            '-Wl,--build-id=sha1',
            '-I' + str(root / 'src/libs/util/include'),
            str(Path(__file__).with_name('check.cpp')),
            '-o',
            str(binary),
        ],
        check=True,
    )
    return binary


def file_id(path):
    notes = subprocess.run(
        ['readelf', '-n', str(path)], capture_output=True, text=True, check=True, env={**os.environ, 'LC_ALL': 'C'}
    ).stdout
    match = re.search(r'Build ID: ([0-9a-f]+)', notes)
    if not match:
        sys.exit(f'FAIL: readelf finds no build ID in {path}')
    return match[1]


with tempfile.TemporaryDirectory(prefix='storm-build-id-') as directory:
    first, again, changed = build(directory, 'a', 1), build(directory, 'b', 1), build(directory, 'c', 2)
    runs = {}
    for binary in (first, again, changed):
        lines = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout.split('\n')
        runs[binary] = (lines[0].split()[0], lines[1])
        if runs[binary][0] != file_id(binary):
            sys.exit(f'FAIL: {binary.name}: read {runs[binary][0]}, the file says {file_id(binary)}')
    libc = runs[first][1]
    if not re.fullmatch(r'[0-9a-f]{16,}', libc) or libc == runs[first][0]:
        sys.exit(f'FAIL: libc build ID {libc!r}')
    if runs[first][0] != runs[again][0]:
        sys.exit('FAIL: an identical rebuild has another build ID')
    if runs[first][0] == runs[changed][0]:
        sys.exit('FAIL: changed code kept its build ID')
print('PASS: module build ID: matches readelf, kept by an identical rebuild, changed with the code, found for libc')
