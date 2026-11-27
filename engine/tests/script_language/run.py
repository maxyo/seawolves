#!/usr/bin/env python3
"""Script language and VM: compile and run tests/script_language/cases/*.c with script_check (src/apps/script_check).

Each case runs every `int test_*()` function, which must return 1 without a runtime error; a case whose first line is
`// expect-error: <text>` must fail to compile with that error. Every case runs twice: compiled, and loaded from a
script cache another compiler wrote. The engine's host build makes script_check; $STORM_SCRIPT_CHECK names it
(NOT_RUN without it).
"""
import os
from pathlib import Path
import subprocess
import sys

tool = os.environ.get('STORM_SCRIPT_CHECK')
if not tool:
    print('NOT_RUN: set STORM_SCRIPT_CHECK to a script_check binary of the engine host build')
    sys.exit(77)
# A sanitizer build of the tool: the compiler's state is freed by ending the process.
env = dict(os.environ, ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS', 'detect_leaks=0'))
cases = sorted(map(str, Path(__file__).resolve().parent.joinpath('cases').glob('*.c')))
plain = subprocess.run([tool, 'test', *cases], env=env).returncode
cached = subprocess.run([tool, 'test', '--cache', *cases], env=env).returncode
sys.exit(plain or cached)
